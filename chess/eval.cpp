#include "eval.hpp"
#include "eval_values.hpp"
#include "endgame.hpp"
#include "assert.hpp"
#include "magic.hpp"
#include "pawn_structure_hash_table.hpp"
#include "util.hpp"
#include "tables.hpp"
#include "zobrist.hpp"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace eval_detail {
enum type {
	material,
	imbalance,
	pst,
	pawn_structure,
	pawn_shield,
	passed_pawns,
	king_attack,
	king_tropism,
	mobility,
	center_control,
	absolute_pins,
	hanging_pieces,
	connected_rooks,
	rooks_on_open_file,
	rooks_on_7h_rank,
	drawishness,
	outposts,
	double_bishop,
	side_to_move,
	trapped_rook,

	max
};
}

namespace {

struct eval_results {
	eval_results()
		: passed_pawns()
	{
		for( int c = 0; c < 2; ++c ) {
			king_pos[c] = 0;

			for( int pi = 0; pi < 7; ++pi ) {
				attacks[c][pi] = 0;
			}

			count_king_attackers[c] = 0;
			king_attacker_sum[c] = 0;
		}
	}

	uint64_t king_pos[2];

	uint64_t attacks[2][7];

	short count_king_attackers[2];
	short king_attacker_sum[2];

	uint64_t passed_pawns;

	// End results
	score eval[2];
};

score detailed_results[eval_detail::max][2];

template<bool detail, eval_detail::type t>
inline void add_score( eval_results& results, color::type c, score const& s ) {
	if( detail ) {
		detailed_results[t][c] += s;
	}
	results.eval[c] += s;
}

template<bool detail, eval_detail::type t>
inline void add_score( eval_results& results, score const* s ) {
	if( detail ) {
		detailed_results[t][0] += s[0];
		detailed_results[t][1] += s[1];
	}
	results.eval[0] += s[0];
	results.eval[1] += s[1];
}

}

short evaluate_move( position const& p, color::type c, move const& m )
{
	score delta = -pst[c][m.piece][m.source];

	if( m.flags & move_flags::castle ) {
		delta += pst[c][pieces::king][m.target];

		unsigned char row = c ? 56 : 0;
		if( m.target % 8 == 6 ) {
			// Kingside
			delta += pst[c][pieces::rook][row + 5] - pst[c][pieces::rook][row + 7];
		}
		else {
			// Queenside
			delta += pst[c][pieces::rook][row + 3] - pst[c][pieces::rook][row];
		}
	}
	else {

		if( m.captured_piece != pieces::none ) {
			if( m.flags & move_flags::enpassant ) {
				unsigned char ep = (m.target & 0x7) | (m.source & 0x38);
				delta += eval_values::material_values[pieces::pawn] + pst[1-c][pieces::pawn][ep];
			}
			else {
				delta += eval_values::material_values[m.captured_piece] + pst[1-c][m.captured_piece][m.target];
			}
		}

		int promotion = m.flags & move_flags::promotion_mask;
		if( promotion ) {
			pieces::type promotion_piece = static_cast<pieces::type>(promotion >> move_flags::promotion_shift);
			delta -= eval_values::material_values[pieces::pawn];
			delta += eval_values::material_values[promotion_piece] + pst[c][promotion_piece][m.target];
		}
		else {
			delta += pst[c][m.piece][m.target];
		}
	}

#if 0
	{
		position p2 = p;
		apply_move( p2, m, c );
		ASSERT( p2.verify() );
		ASSERT( p.base_eval + delta == p2.base_eval );
	}
#endif

	return delta.scale( p.material[0].mg() + p.material[1].mg() );
}


uint64_t const central_squares[2] = { 0x000000003c3c3c00ull, 0x003c3c3c00000000ull };

namespace {


template<bool detail>
inline static void evaluate_pawns_mobility( position const& p, color::type c, eval_results& results )
{
	uint64_t pawns = p.bitboards[c].b[bb_type::pawns];

	while( pawns ) {
		uint64_t pawn = bitscan_unset( pawns );

		add_score<detail, eval_detail::king_tropism>( results, c, eval_values::tropism[pieces::pawn] * proximity[pawn][results.king_pos[1-c]] );

		uint64_t pc = pawn_control[c][pawn];

		results.attacks[c][pieces::pawn] |= pc;

		pc &= ~p.bitboards[c].b[bb_type::all_pieces];

		if( pc & king_attack_zone[1-c][results.king_pos[1-c]] ) {
			++results.count_king_attackers[c];
			results.king_attacker_sum[c] += eval_values::king_attack_by_piece[pieces::pawn];
		}
	}
}

template<bool detail>
inline static void evaluate_knight_mobility( position const& p, color::type c, uint64_t knight, eval_results& results )
{
	uint64_t moves = possible_knight_moves[knight];

	results.attacks[c][pieces::knight] |= moves;

	if( moves & king_attack_zone[1-c][results.king_pos[1-c]] ) {
		++results.count_king_attackers[c];
		results.king_attacker_sum[c] += eval_values::king_attack_by_piece[pieces::knight];
	}

	moves &= ~(p.bitboards[c].b[bb_type::all_pieces] | p.bitboards[1-c].b[bb_type::pawn_control]);
	add_score<detail, eval_detail::mobility>( results, c, eval_values::mobility_knight[popcount(moves)] );
}


short outpost_squares[64] = {
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 1, 1, 1, 1, 1, 1, 0,
	0, 1, 1, 1, 1, 1, 1, 0,
	0, 1, 1, 1, 1, 1, 1, 0,
	0, 1, 1, 1, 1, 1, 1, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};


template<bool detail>
inline static void evaluate_knight_outpost( position const& p, color::type c, uint64_t knight, eval_results& results )
{
	if( outpost_squares[knight] && !(p.bitboards[1-c].b[bb_type::pawns] & forward_pawn_attack[c][knight] ) ) {
		// We're in a sunny outpost. Check whether theres a pawn to cover us
		if( (1ull << knight) & p.bitboards[c].b[bb_type::pawn_control] ) {
			add_score<detail, eval_detail::outposts>( results, c, eval_values::knight_outposts[1] );
		}
		else {
			add_score<detail, eval_detail::outposts>( results, c, eval_values::knight_outposts[0] );
		}
	}
}


template<bool detail>
inline static void evaluate_knights( position const& p, color::type c, eval_results& results )
{
	uint64_t knights = p.bitboards[c].b[bb_type::knights];

	while( knights ) {
		uint64_t knight = bitscan_unset( knights );

		add_score<detail, eval_detail::king_tropism>( results, c, eval_values::tropism[pieces::knight] * proximity[knight][results.king_pos[1-c]] );

		evaluate_knight_mobility<detail>( p, c, knight, results );
		evaluate_knight_outpost<detail>( p, c, knight, results );
	}

}


template<bool detail>
inline static void evaluate_bishop_mobility( position const& p, color::type c, uint64_t bishop, eval_results& results )
{
	uint64_t const all_blockers = (p.bitboards[1-c].b[bb_type::all_pieces] | p.bitboards[c].b[bb_type::all_pieces]) & ~p.bitboards[c].b[bb_type::queens];

	uint64_t moves = bishop_magic( bishop, all_blockers );

	if( moves & king_attack_zone[1-c][results.king_pos[1-c]] ) {
		++results.count_king_attackers[c];
		results.king_attacker_sum[c] += eval_values::king_attack_by_piece[pieces::bishop];
	}

	results.attacks[c][pieces::bishop] |= moves;

	moves &= ~(p.bitboards[c].b[bb_type::all_pieces] | p.bitboards[1-c].b[bb_type::pawn_control]);
	add_score<detail, eval_detail::mobility>( results, c, eval_values::mobility_bishop[popcount(moves)] );
}


template<bool detail>
inline static void evaluate_bishop_pin( position const& p, color::type c, uint64_t bishop, eval_results& results )
{
	uint64_t own_blockers = p.bitboards[c].b[bb_type::all_pieces];
	uint64_t unblocked_moves = bishop_magic( bishop, own_blockers );

	if( unblocked_moves & p.bitboards[1-c].b[bb_type::king] ) {

		uint64_t between = between_squares[bishop][results.king_pos[1-c]] & p.bitboards[1-c].b[bb_type::all_pieces];
		if( popcount( between ) == 1 ) {
			pieces::type piece = get_piece_on_square( p, static_cast<color::type>(1-c), bitscan( between ) );
			add_score<detail, eval_detail::absolute_pins>( results, c, eval_values::absolute_pin[ piece ] );
		}
	}
}



template<bool detail>
inline static void evaluate_bishop_outpost( position const& p, color::type c, uint64_t bishop, eval_results& results )
{
	if( outpost_squares[bishop] && !(p.bitboards[1-c].b[bb_type::pawns] & forward_pawn_attack[c][bishop] ) ) {
		// We're in a sunny outpost. Check whether theres a pawn to cover us
		if( (1ull << bishop) & p.bitboards[c].b[bb_type::pawn_control] ) {
			add_score<detail, eval_detail::outposts>( results, c, eval_values::bishop_outposts[1] );
		}
		else {
			add_score<detail, eval_detail::outposts>( results, c, eval_values::bishop_outposts[0] );
		}
	}
}


template<bool detail>
inline static void evaluate_bishops( position const& p, color::type c, eval_results& results )
{
	uint64_t bishops = p.bitboards[c].b[bb_type::bishops];

	while( bishops ) {
		uint64_t bishop = bitscan_unset( bishops );

		add_score<detail, eval_detail::king_tropism>( results, c, eval_values::tropism[pieces::bishop] * proximity[bishop][results.king_pos[1-c]] );

		evaluate_bishop_mobility<detail>( p, c, bishop, results );
		evaluate_bishop_pin<detail>( p, c, bishop, results );
		evaluate_bishop_outpost<detail>( p, c, bishop, results );
	}
}


template<bool detail>
inline static void evaluate_rook_mobility( position const& p, color::type c, uint64_t rook, eval_results& results )
{
	uint64_t const all_blockers = (p.bitboards[1-c].b[bb_type::all_pieces] | p.bitboards[c].b[bb_type::all_pieces]) & ~p.bitboards[c].b[bb_type::rooks];

	uint64_t moves = rook_magic( rook, all_blockers );

	if( moves & king_attack_zone[1-c][results.king_pos[1-c]] ) {
		++results.count_king_attackers[c];
		results.king_attacker_sum[c] += eval_values::king_attack_by_piece[pieces::rook];
	}

	results.attacks[c][pieces::rook] |= moves;

	if( moves & p.bitboards[c].b[bb_type::rooks] ) {
		// Is it bad that queen might be inbetween due to the X-RAY?
		add_score<detail, eval_detail::connected_rooks>( results, c, eval_values::connected_rooks );
	}

	moves &= ~(p.bitboards[c].b[bb_type::all_pieces] | p.bitboards[1-c].b[bb_type::pawn_control]);
	add_score<detail, eval_detail::mobility>( results, c, eval_values::mobility_rook[popcount(moves)] );
}


template<bool detail>
inline static void evaluate_rook_pin( position const& p, color::type c, uint64_t rook, eval_results& results )
{
	uint64_t own_blockers = p.bitboards[c].b[bb_type::all_pieces];
	uint64_t unblocked_moves = rook_magic( rook, own_blockers );

	if( unblocked_moves & p.bitboards[1-c].b[bb_type::king] ) {

		uint64_t between = between_squares[rook][results.king_pos[1-c]] & p.bitboards[1-c].b[bb_type::all_pieces];
		if( popcount( between ) == 1 ) {
			pieces::type piece = get_piece_on_square( p, static_cast<color::type>(1-c), bitscan( between ) );
			add_score<detail, eval_detail::absolute_pins>( results, c, eval_values::absolute_pin[ piece ] );
		}
	}
}


template<bool detail>
inline static void evaluate_rook_on_open_file( position const& p, color::type c, uint64_t rook, eval_results& results )
{
	uint64_t file = 0x0101010101010101ull << (rook % 8);
	if( !(p.bitboards[c].b[bb_type::pawns] & file) ) {
		if( p.bitboards[1-c].b[bb_type::pawns] & file ) {
			add_score<detail, eval_detail::rooks_on_open_file>( results, c, eval_values::rooks_on_half_open_file );
		}
		else {
			add_score<detail, eval_detail::rooks_on_open_file>( results, c, eval_values::rooks_on_open_file );
		}
	}
}


uint64_t const trapped_king[2]   = { 0x00000000000000ffull, 0xff00000000000000ull };
uint64_t const trapping_piece[2] = { 0x00ff000000000000ull, 0x000000000000ff00ull };

template<bool detail>
inline static void evaluate_rooks( position const& p, color::type c, eval_results& results )
{
	uint64_t rooks = p.bitboards[c].b[bb_type::rooks];

	while( rooks ) {
		uint64_t rook = bitscan_unset( rooks );

		add_score<detail, eval_detail::king_tropism>( results, c, eval_values::tropism[pieces::rook] * proximity[rook][results.king_pos[1-c]] );

		evaluate_rook_mobility<detail>( p, c, rook, results);
		evaluate_rook_pin<detail>( p, c, rook, results );
		evaluate_rook_on_open_file<detail>( p, c, rook, results );
	}

	// At least in endgame, enemy king trapped on its home rank is quite useful.
	if( (p.bitboards[c].b[bb_type::rooks] | p.bitboards[c].b[bb_type::queens]) & trapping_piece[c] && p.bitboards[1-c].b[bb_type::king] & trapped_king[1-c] ) {
		add_score<detail, eval_detail::rooks_on_7h_rank>( results, c, eval_values::rooks_on_rank_7 );
	}

	// In middle-game, a rook trapped behind own pawns on the same side of a king that cannot castle is quite bad.
	// Getting that rook into play likely requires several tempi and/or might destroy structur of own position.
	// TODO
}


template<bool detail>
inline static void evaluate_queen_mobility( position const& p, color::type c, uint64_t queen, eval_results& results )
{
	uint64_t const all_blockers = p.bitboards[1-c].b[bb_type::all_pieces] | p.bitboards[c].b[bb_type::all_pieces];

	uint64_t moves = bishop_magic( queen, all_blockers ) | rook_magic( queen, all_blockers );

	if( moves & king_attack_zone[1-c][results.king_pos[1-c]] ) {
		++results.count_king_attackers[c];
		results.king_attacker_sum[c] += eval_values::king_attack_by_piece[pieces::queen];
	}

	results.attacks[c][pieces::queen] |= moves;

	moves &= ~(p.bitboards[c].b[bb_type::all_pieces] | p.bitboards[1-c].b[bb_type::pawn_control]);
	add_score<detail, eval_detail::mobility>( results, c, eval_values::mobility_queen[popcount(moves)] );
}


template<bool detail>
inline static void evaluate_queen_pin( position const& p, color::type c, uint64_t queen, eval_results& results )
{
	uint64_t own_blockers = p.bitboards[c].b[bb_type::all_pieces];
	uint64_t unblocked_moves = bishop_magic( queen, own_blockers ) | rook_magic( queen, own_blockers );

	if( unblocked_moves & p.bitboards[1-c].b[bb_type::king] ) {

		uint64_t between = between_squares[queen][results.king_pos[1-c]] & p.bitboards[1-c].b[bb_type::all_pieces];
		if( popcount( between ) == 1 ) {
			pieces::type piece = get_piece_on_square( p, static_cast<color::type>(1-c), bitscan( between ) );
			add_score<detail, eval_detail::absolute_pins>( results, c, eval_values::absolute_pin[ piece ] );
		}
	}
}


template<bool detail>
inline static void evaluate_queens( position const& p, color::type c, eval_results& results )
{
	uint64_t queens = p.bitboards[c].b[bb_type::queens];

	while( queens ) {
		uint64_t queen = bitscan_unset( queens );

		add_score<detail, eval_detail::king_tropism>( results, c, eval_values::tropism[pieces::queen] * proximity[queen][results.king_pos[1-c]] );

		evaluate_queen_mobility<detail>( p, c, queen, results );
		evaluate_queen_pin<detail>( p, c, queen, results );
	}
}


short advance_bonus[] = { 1, 1, 1, 2, 4, 8 };

template<bool detail>
void evaluate_passed_pawns( position const& p, color::type c, eval_results& results )
{
	for( int i = 0; i < 2; ++i ) {
		uint64_t passed = (p.bitboards[i].b[bb_type::pawns] & results.passed_pawns );
		uint64_t unstoppable = passed & ~rule_of_the_square[1-i][c][results.king_pos[1-i]];

		while( passed ) {
			uint64_t pawn = bitscan_unset( passed );

			uint64_t file = pawn % 8;

			short advance = i ? (6 - pawn / 8) : (pawn / 8 - 1);

			add_score<detail, eval_detail::passed_pawns>( results, static_cast<color::type>(i), (eval_values::passed_pawn_king_distance[0] * king_distance[pawn + (i ? -8 : 8)][results.king_pos[1-i]] - eval_values::passed_pawn_king_distance[1] * king_distance[pawn + (i ? -8 : 8)][results.king_pos[i]]) * advance_bonus[advance] );

			add_score<detail, eval_detail::passed_pawns>( results, static_cast<color::type>(i), eval_values::advanced_passed_pawn[file][advance] );

			uint64_t forward_squares = doubled_pawns[i][pawn];

			if( (1ull << pawn) & unstoppable ) {
				add_score<detail, eval_detail::passed_pawns>( results, static_cast<color::type>(i), eval_values::rule_of_the_square * advance_bonus[advance] );
			}
			uint64_t hinderers = (results.attacks[1-i][0] & ~results.attacks[i][0]) | p.bitboards[1-i].b[bb_type::all_pieces];
			if( !(forward_squares & hinderers) ) {
				add_score<detail, eval_detail::passed_pawns>( results, static_cast<color::type>(i), eval_values::passed_pawn_unhindered * advance_bonus[advance] );
			}
		}
	}
}

short base_king_attack[2][8] = {
	{
		0, 1, 4, 10, 12, 12, 12, 12
	},
	{
		12, 12, 12, 12, 10, 4, 1, 0
	}
};


template<bool detail>
static void evaluate_king_attack( position const& p, color::type c, color::type to_move, eval_results& results )
{
	// Consider a lone attacker harmless
	if( results.count_king_attackers[c] < 2 ) {
		return;
	}

	short attack = base_king_attack[1-c][results.king_pos[1-c] / 8];

	// Add all the attackers that can reach close to the king
	attack += results.king_attacker_sum[c];

	// Add all positions where a piece can safely give check
	uint64_t secure_squares = ~(results.attacks[1-c][0] | p.bitboards[c].b[bb_type::all_pieces]); // Don't remove enemy pieces, those we would just capture

	uint64_t knight_checks = possible_knight_moves[ results.king_pos[1-c] ] & secure_squares;
	uint64_t const all_blockers = p.bitboards[1-c].b[bb_type::all_pieces] | p.bitboards[c].b[bb_type::all_pieces];
	uint64_t bishop_checks = bishop_magic( results.king_pos[1-c], all_blockers ) & secure_squares;
	uint64_t rook_checks = rook_magic( results.king_pos[1-c], all_blockers ) & secure_squares;

	attack += popcount( results.attacks[c][pieces::knight] & knight_checks ) * eval_values::king_check_by_piece[pieces::knight];
	attack += popcount( results.attacks[c][pieces::bishop] & bishop_checks ) * eval_values::king_check_by_piece[pieces::bishop];
	attack += popcount( results.attacks[c][pieces::rook] & rook_checks ) * eval_values::king_check_by_piece[pieces::rook];
	attack += popcount( results.attacks[c][pieces::queen] & (bishop_checks|rook_checks) ) * eval_values::king_check_by_piece[pieces::queen];

	// Rooks and queens that are able to move next to a king without getting captured are extremely dangerous.
	uint64_t undefended = possible_king_moves[results.king_pos[1-c]] & ~(results.attacks[1-c][pieces::pawn] | results.attacks[1-c][pieces::knight] | results.attacks[1-c][pieces::bishop] | results.attacks[1-c][pieces::rook] | results.attacks[1-c][pieces::queen]);
	uint64_t backed_queen_attacks = results.attacks[c][pieces::queen] & (results.attacks[c][pieces::pawn] | results.attacks[c][pieces::knight] | results.attacks[c][pieces::bishop] | results.attacks[c][pieces::rook]);
	uint64_t backed_rook_attacks = results.attacks[c][pieces::rook] & (results.attacks[c][pieces::pawn] | results.attacks[c][pieces::knight] | results.attacks[c][pieces::bishop] | results.attacks[c][pieces::queen]);

	uint64_t king_melee_attack_by_queen = undefended & backed_queen_attacks & ~p.bitboards[c].b[bb_type::all_pieces];
	uint64_t king_melee_attack_by_rook = undefended & backed_rook_attacks & ~p.bitboards[c].b[bb_type::all_pieces] & rook_magic( results.king_pos[1-c], 0 );

	short initiative = (to_move == c) ? 2 : 1;
	attack += popcount( king_melee_attack_by_queen ) * eval_values::king_melee_attack_by_queen * initiative;
	attack += popcount( king_melee_attack_by_rook ) * eval_values::king_melee_attack_by_rook * initiative;

	int offset = (std::min)( short(199), attack );

	add_score<detail, eval_detail::king_attack>( results, c, eval_values::king_attack[offset] );
}


void evaluate_pawn( uint64_t own_pawns, uint64_t foreign_pawns, color::type c, uint64_t pawn, eval_results& results, score* s )
{
	uint64_t file = pawn % 8;

	// Unfortunately this is getting too complex for me to make branchless
	bool doubled = doubled_pawns[c][pawn] & own_pawns;
	if( doubled ) {
		s[c] += eval_values::doubled_pawn[file];
	}

	bool connected = connected_pawns[c][pawn] & own_pawns;
	if( connected ) {
		s[c] += eval_values::connected_pawn[file];
	}

	bool isolated = !(isolated_pawns[pawn] & own_pawns);
	if( isolated ) {
		s[c] += eval_values::isolated_pawn[file];
	}

	bool passed = !(passed_pawns[c][pawn] & foreign_pawns);
	if( passed && !doubled ) {
		results.passed_pawns |= 1ull << pawn;
	}

	bool backwards = false;
	if( !passed && !connected && !isolated && !(forward_pawn_attack[1-c][pawn] & own_pawns ) ) {
		uint64_t opposition = forward_pawn_attack[c][pawn] & foreign_pawns;
		if( opposition ) {
			backwards = true;
		}
	}

	if( backwards ) {
		s[c] += eval_values::backward_pawn[file];
	}

	bool candidate = false;
	if( !passed && !isolated && !backwards ) {
		// Candidate passer
		uint64_t file = 0x0101010101010101ull << (pawn % 8);
		uint64_t opposition = passed_pawns[c][pawn] & foreign_pawns;
		uint64_t support = forward_pawn_attack[1-c][pawn + (c ? -8 : 8)] & own_pawns;
		if( !(file & foreign_pawns) && popcount(opposition) <= popcount(support) ) {
			candidate = true;
		}
	}

	if( candidate ) {
		s[c] += eval_values::candidate_passed_pawn[file];
	}

#if 0
	std::cerr << "Pawn: " << std::setw(2) << pawn
			  << " Color: " << c
			  << " Doubled: " << doubled
			  << " Isolated: " << isolated
			  << " Connected: " << connected
			  << " Passed: " << passed
			  << " Candidate: " << candidate
			  << " Backwards: " << backwards
			  << std::endl;
#endif

}


template<bool detail>
void evaluate_pawns( position const& p, eval_results& results )
{
	score s[2];
	if( pawn_hash_table.lookup( p.pawn_hash, s, results.passed_pawns ) ) {
		add_score<detail, eval_detail::pawn_structure>( results, s );
		return;
	}

	for( int c = 0; c < 2; ++c ) {
		uint64_t own_pawns = p.bitboards[c].b[bb_type::pawns];
		uint64_t foreign_pawns = p.bitboards[1-c].b[bb_type::pawns];

		uint64_t pawns = own_pawns;
		while( pawns ) {
			uint64_t pawn = bitscan_unset( pawns );

			evaluate_pawn( own_pawns, foreign_pawns, static_cast<color::type>(c), pawn, results, s );
		}
	}

	add_score<detail, eval_detail::pawn_structure>( results, s );
	pawn_hash_table.store( p.pawn_hash, s, results.passed_pawns );
}


/* Pawn shield for king
 * 1 2 3
 * 4 5 6
 *   K
 * Pawns on 1, 3, 4, 6 are awarded shield_const points, pawns on 2 and 5 are awarded twice as many points.
 * Double pawns do not count.
 *
 * Special case: a and h file. There b and g also count twice.
 */
template<bool detail>
void evaluate_pawn_shield( position const& p, eval_results& results )
{
	for( unsigned int c = 0; c < 2; ++c ) {
		uint64_t shield = king_pawn_shield[c][results.king_pos[c]] & p.bitboards[c].b[bb_type::pawns];

		add_score<detail, eval_detail::pawn_shield>( results, static_cast<color::type>(c), eval_values::pawn_shield * static_cast<short>(popcount(shield)) );
	}
}


template<bool detail>
static void evaluate_drawishness( position const& p, color::type c, eval_results& results )
{
	if( p.bitboards[c].b[bb_type::pawns] ) {
		return;
	}

	short material_difference = p.material[c].eg() - p.material[1-c].eg();
	if( material_difference > 0 && material_difference <= eval_values::insufficient_material_threshold ) {
		add_score<detail, eval_detail::drawishness>( results, c, score( 0, eval_values::drawishness ) );
	}
}


template<bool detail>
static void evaluate_center( position const& p, color::type c, eval_results& results )
{
	// Not taken by own pawns nor under control by enemy pawns
	uint64_t potential_center_squares = central_squares[c] & ~(p.bitboards[c].b[bb_type::pawns] | p.bitboards[1-c].b[bb_type::pawn_control]);
	uint64_t safe_center_squares = potential_center_squares & (results.attacks[c][0] | ~results.attacks[1-c][0]);

	add_score<detail, eval_detail::center_control>( results, c, eval_values::center_control * popcount(safe_center_squares) );
}

template<bool detail>
static void do_evaluate( position const& p, color::type to_move, eval_results& results )
{
	results.king_pos[0] = bitscan( p.bitboards[0].b[bb_type::king] );
	results.king_pos[1] = bitscan( p.bitboards[1].b[bb_type::king] );

	evaluate_pawns<detail>( p, results );

	for( unsigned int c = 0; c < 2; ++c ) {
		if( popcount( p.bitboards[c].b[bb_type::bishops] ) > 1 ) {
			add_score<detail, eval_detail::double_bishop>( results, static_cast<color::type>(c), eval_values::double_bishop );
		}
	}

	// Todo: Make truly phased.
	short mat[2];
	mat[0] = static_cast<short>( popcount( p.bitboards[0].b[bb_type::all_pieces] ^ p.bitboards[0].b[bb_type::pawns] ) );
	mat[1] = static_cast<short>( popcount( p.bitboards[1].b[bb_type::all_pieces] ^ p.bitboards[1].b[bb_type::pawns] ) );
	short diff = mat[0] - mat[1];
	add_score<detail, eval_detail::imbalance>( results, color::white, eval_values::material_imbalance * diff );

	evaluate_pawn_shield<detail>( p, results );

	add_score<detail, eval_detail::side_to_move>( results, to_move, eval_values::side_to_move );

	for( unsigned int c = 0; c < 2; ++c ) {
		evaluate_pawns_mobility<detail>( p, static_cast<color::type>(c), results );
		evaluate_knights<detail>( p, static_cast<color::type>(c), results );
		evaluate_bishops<detail>( p, static_cast<color::type>(c), results );
		evaluate_rooks<detail>( p, static_cast<color::type>(c), results );
		evaluate_queens<detail>( p, static_cast<color::type>(c), results );

		//Piece-square tables already contain this
		//results.center_control += static_cast<short>(popcount(p.bitboards[c].b[bb_type::all_pieces] & center_squares));

		results.attacks[c][pieces::none] =
				results.attacks[c][pieces::pawn] |
				results.attacks[c][pieces::knight] |
				results.attacks[c][pieces::bishop] |
				results.attacks[c][pieces::rook] |
				results.attacks[c][pieces::queen] |
				possible_king_moves[results.king_pos[c]];
	}

	for( unsigned int c = 0; c < 2; ++c ) {
		// Bonus for enemy pieces we attack that are not defended
		// Undefended are those attacked by self, not attacked by enemy
		uint64_t undefended = results.attacks[c][pieces::none] & ~results.attacks[1-c][pieces::none];
		for( unsigned int piece = 1; piece < 6; ++piece ) {
			add_score<detail, eval_detail::hanging_pieces>( results, static_cast<color::type>(c), eval_values::hanging_piece[piece] * popcount( p.bitboards[1-c].b[piece] & undefended ) );
		}

		evaluate_king_attack<detail>( p, static_cast<color::type>(c), to_move, results );

		evaluate_drawishness<detail>( p, static_cast<color::type>(c), results );

		evaluate_center<detail>( p, static_cast<color::type>(c), results );
	}

	evaluate_passed_pawns<detail>( p, to_move, results );
}

score sum_up( position const& p, eval_results const& results ) {
	return p.base_eval + results.eval[color::white] - results.eval[color::black];
}
}

namespace {
static short scale( position const& p, score const& s )
{
	short mat = p.material[0].mg() + p.material[1].mg();
	return s.scale( mat );
}


static std::string explain( position const& p, const char* name, score const& data ) {
	std::stringstream ss;
	ss << std::setw(19) << name << " |             |             | " << std::setw(5) << data.mg() << " " << std::setw(5) << data.eg() << " " << std::setw(5) << scale( p, data ) << std::endl;
	return ss.str();
}


static std::string explain( position const& p, const char* name, score const* data ) {
	std::stringstream ss;
	ss << std::setw(19) << name << " | ";
	ss << std::setw(5) << data[0].mg() << " " << std::setw(5) << data[0].eg() << " | ";
	ss << std::setw(5) << data[1].mg() << " " << std::setw(5) << data[1].eg() << " | ";
	score total = data[0] - data[1];
	ss << std::setw(5) << total.mg() << " " << std::setw(5) << total.eg() << " " << std::setw(5) << scale( p, data[0]-data[1] ) << std::endl;
	return ss.str();
}


static std::string explain( position const& p, const char* name, eval_detail::type t ) {
	std::stringstream ss;

	score s[2] = detailed_results[t];
	if( s[0] != score() || s[1] != score() ) {
		ss << std::setw(19) << name << " | ";
		ss << std::setw(5) << s[0].mg() << " " << std::setw(5) << s[0].eg() << " | ";
		ss << std::setw(5) << s[1].mg() << " " << std::setw(5) << s[1].eg() << " | ";
		score total = s[0] - s[1];
		ss << std::setw(5) << total.mg() << " " << std::setw(5) << total.eg() << " " << std::setw(5) << scale( p, total ) << std::endl;
	}
	return ss.str();
}


}


std::string explain_eval( position const& p, color::type c )
{
	std::stringstream ss;

	short endgame = 0;
	if( evaluate_endgame( p, endgame ) ) {
		ss << "Endgame: " << endgame << std::endl;
	}
	else {
		for( int i = 0; i < eval_detail::max; ++i ) {
			detailed_results[i][0] = score();
			detailed_results[i][1] = score();
		}

		eval_results results;
		do_evaluate<true>( p, c, results );

		score full = sum_up( p, results );

		ss << "                    |    White    |    Black    |       Total" << std::endl;
		ss << "         Term       |   MG   EG   |   MG   EG   |   MG   EG  Scaled" << std::endl;
		ss << "===================================================================" << std::endl;
		ss << explain( p, "Material", p.material );
		ss << explain( p, "Imbalance", detailed_results[eval_detail::imbalance][0] );
		ss << explain( p, "PST", p.base_eval-p.material[0]+p.material[1] );
		ss << explain( p, "Pawn structure", eval_detail::pawn_structure );
		ss << explain( p, "Pawn shield", eval_detail::pawn_shield );
		ss << explain( p, "Passed pawns", eval_detail::passed_pawns );
		ss << explain( p, "King attack", eval_detail::king_attack );
		ss << explain( p, "King tropism", eval_detail::king_tropism );
		ss << explain( p, "Mobility",  eval_detail::mobility );
		ss << explain( p, "Center control", eval_detail::center_control );
		ss << explain( p, "Absolute pins", eval_detail::absolute_pins );
		ss << explain( p, "Hanging pieces", eval_detail::hanging_pieces );
		ss << explain( p, "Connected rooks", eval_detail::connected_rooks );
		ss << explain( p, "Rooks on open file", eval_detail::rooks_on_open_file );
		ss << explain( p, "Rooks on 7th rank", eval_detail::rooks_on_7h_rank );
		ss << explain( p, "Trapped rook", eval_detail::trapped_rook );
		ss << explain( p, "Outposts", eval_detail::outposts );
		ss << explain( p, "Double bishop", eval_detail::double_bishop );
		ss << explain( p, "Side to move", eval_detail::side_to_move );
		ss << explain( p, "Drawishness", eval_detail::drawishness );

		ss << "===================================================================" << std::endl;
		ss << explain( p, "Total", full );
	}

	return ss.str();
}


short evaluate_full( position const& p, color::type c )
{
	short eval = 0;
	if( evaluate_endgame( p, eval ) ) {
		if( c ) {
			eval = -eval;
		}
		return eval;
	}

	eval_results results;
	do_evaluate<false>( p, c, results );

	score full = sum_up( p, results );
	
	eval = scale( p, full );
	if( c ) {
		eval = -eval;
	}

	ASSERT( eval > result::loss && eval < result::win );

	return eval;
}
