#include "chess.hpp"
#include "assert.hpp"
#include "calc.hpp"
#include "detect_check.hpp"
#include "eval.hpp"
#include "moves.hpp"
#include "util.hpp"
#include "random.hpp"
#include "pawn_structure_hash_table.hpp"
#include "platform.hpp"
#include "zobrist.hpp"

#include <iostream>

bool validate_move( position const& p, move const& m, color::type c )
{
	check_map check;
	calc_check_map( p, c, check );

	move_info moves[200];
	move_info* pm = moves;
	int ev = evaluate_fast( p, c );
	calculate_moves( p, c, ev, pm, check, killer_moves() );

	for( move_info* it = moves; it != pm; ++it ) {
		if( it->m == m ) {
			return true;
		}
	}

	return false;
}

bool parse_move( position& p, color::type c, std::string const& line, move& m )
{
	std::string str = line;
	std::size_t len = str.size();
	if( len && str[len - 1] == '#' ) {
		--len;
		str = str.substr(0, len);
	}
	else if( len && str[len - 1] == '+' ) {
		--len;
		str = str.substr(0, len);
	}

	bool promotion = false;
	if( len && (str[len - 1] == 'q' || str[len - 1] == 'r' || str[len - 1] == 'b' || str[len - 1] == 'n') ) {
		promotion = true;
		--len;
		str = str.substr(0, len);
	}

	if( str == "0-0" || str == "O-O" ) {
		m.source_col = 4;
		m.target_col = 6;
		m.source_row = c ? 7 : 0;
		m.target_row = m.source_row;
		if( !validate_move( p, m, c ) ) {
			std::cout << "Illegal move (not valid): " << line << std::endl;
			return false;
		}
		return true;
	}
	else if( str == "0-0-0" || str == "O-O-O" ) {
		m.source_col = 4;
		m.target_col = 2;
		m.source_row = c ? 7 : 0;
		m.target_row = m.source_row;
		if( !validate_move( p, m, c ) ) {
			std::cout << "Illegal move (not valid): " << line << std::endl;
			return false;
		}
		return true;
	}
	unsigned char piecetype = 0;

	if( len && str[len - 1] == 'Q' ) {
		--len;
		str = str.substr(0, len);
		if( str[len - 1] == '=' ) {
			--len;
			str = str.substr(0, len);
		}
		piecetype = 'P';
	}

	const char* s = str.c_str();

	switch( *s ) {
	case 'B':
	case 'Q':
	case 'K':
	case 'R':
	case 'N':
	case 'P':
		if( piecetype ) {
			std::cout << "Error (unknown command): " << line << std::endl;
			return false;
		}
		piecetype = *(s++);
		break;
	}

	int first_col = -1;
	int first_row = -1;
	int second_col = -1;
	int second_row = -1;

	bool got_separator = false;
	bool capture = false; // Not a capture or unknown

	while( *s ) {
		if( *s == 'x' || *s == ':' || *s == '-' ) {
			if( got_separator ) {
				std::cout << "Error (unknown command): " << line << std::endl;
				return false;
			}
			got_separator = true;
			if( *s == 'x' ) {
				capture = true;
			}
		}
		else if( *s >= 'a' && *s <= 'h' ) {
			if( second_col != -1 ) {
				std::cout << "Error (unknown command): " << line << std::endl;
				return false;
			}
			if( !got_separator && first_row == -1 && first_col == -1 ) {
				first_col = *s - 'a';
			}
			else {
				second_col = *s - 'a';
			}
		}
		else if( *s >= '1' && *s <= '8' ) {
			if( second_row != -1 ) {
				std::cout << "Error (unknown command): " << line << std::endl;
				return false;
			}
			if( !got_separator && second_col == -1 && first_row == -1 ) {
				first_row = *s - '1';
			}
			else {
				second_row = *s - '1';
			}
		}
		else {
			std::cout << "Error (unknown command): " << line << std::endl;
			return false;
		}

		++s;
	}

	if( !piecetype && (first_col == -1 || second_col == -1 || first_row == -1 || second_col == -1) ) {
		piecetype = 'P';
	}

	if( !got_separator && second_col == -1 && second_row == -1 ) {
		second_col = first_col;
		first_col = -1;
		second_row = first_row;
		first_row = -1;
	}

	if( first_col == -1 && first_row == -1 && second_col == -1 && second_row == -1 ) {
		std::cout << "Error (unknown command): " << line << std::endl;
		return false;
	}

	check_map check;
	calc_check_map( p, c, check );

	move_info moves[200];
	move_info* pm = moves;
	int ev = evaluate_fast( p, c );
	calculate_moves( p, c, ev, pm, check, killer_moves() );

	move_info* match = 0;

	for( move_info* it = moves; it != pm; ++it ) {
		unsigned char source = p.board[it->m.source_col][it->m.source_row] & 0xf;

		char source_piece = 0;

		switch( source ) {
		case pieces::king:
			source_piece = 'K';
			break;
		case pieces::queen:
			source_piece = 'Q';
			break;
		case pieces::rook1:
		case pieces::rook2:
			source_piece = 'R';
			break;
		case pieces::bishop1:
		case pieces::bishop2:
			source_piece = 'B';
			break;
		case pieces::knight1:
		case pieces::knight2:
			source_piece = 'N';
			break;
		case pieces::pawn1:
		case pieces::pawn2:
		case pieces::pawn3:
		case pieces::pawn4:
		case pieces::pawn5:
		case pieces::pawn6:
		case pieces::pawn7:
		case pieces::pawn8:
			if( p.pieces[c][source].special ) {
				unsigned short promoted = (p.promotions[c] >> (2 * (source - pieces::pawn1) ) ) & 0x03;
				switch( promoted ) {
				case promotions::queen:
					source_piece = 'Q';
					break;
				case promotions::rook:
					source_piece = 'R';
					break;
				case promotions::bishop:
					source_piece = 'B';
					break;
				case promotions::knight:
					source_piece = 'N';
					break;
				}
			}
			else {
				source_piece = 'P';
			}
			break;
		}

		if( piecetype && piecetype != source_piece ) {
			continue;
		}

		if( capture && p.board[it->m.target_col][it->m.target_row] == pieces::nil ) {
			// Could still be enpassant
			if( source_piece != 'P' || p.can_en_passant == pieces::nil || (p.can_en_passant >> 4) == c ) {
				continue;
			}

			piece const& ep = p.pieces[1-c][p.can_en_passant & 0x0f];
			if( ep.row != it->m.source_row || ep.column != it->m.target_col ) {
				continue;
			}
		}

		if( first_col != -1 && first_col != it->m.source_col ) {
			continue;
		}
		if( first_row != -1 && first_row != it->m.source_row ) {
			continue;
		}
		if( second_col != -1 && second_col != it->m.target_col ) {
			continue;
		}
		if( second_row != -1 && second_row != it->m.target_row ) {
			continue;
		}

		if( match ) {
			std::cout << "Illegal move (ambigious): " << line << std::endl;
			return false;
		}
		else {
			match = it;
		}
	}


	if( !match ) {
		std::cout << "Illegal move (not valid): " << line << std::endl;
		std::cerr << "Parsed: " << first_col << " " << first_row << " " << second_col << " " << second_row << ", capture=" << capture << std::endl;
		return false;
	}

	if( promotion && match->m.target_row != (c ? 0 : 7) ) {
		std::cout << "Illegal move (not valid, expecting a promotion): " << line << std::endl;
	}

	m = match->m;

	return true;
}

std::string move_to_string( position const& p, color::type c, move const& m )
{
	std::string ret;

	unsigned char source = p.board[m.source_col][m.source_row];

	if( (source >> 4 ) != c ) {
		std::cerr << "FAIL! Invalid move: "
				  << c << " "
				  << (source >> 4) << " "
				  << (source & 0xf) << " "
				  << static_cast<int>(m.source_col) << " "
				  << static_cast<int>(m.source_row) << " "
				  << static_cast<int>(m.target_col) << " "
				  << static_cast<int>(m.target_row) << " "
				  << std::endl;
	}

	source &= 0x0f;
	if( source == pieces::king ) {
		if( m.target_col == 6 && p.pieces[c][source].column == 4 ) {
			return "   O-O  ";
		}
		else if( m.target_col == 2 && p.pieces[c][source].column == 4 ) {
			return " O-O-O  ";
		}
	}

	switch( source ) {
		case pieces::bishop1:
		case pieces::bishop2:
			ret += 'B';
			break;
		case pieces::knight1:
		case pieces::knight2:
			ret += 'N';
			break;
		case pieces::queen:
			ret += 'Q';
			break;
		case pieces::king:
			ret += 'K';
			break;
		case pieces::rook1:
		case pieces::rook2:
			ret += 'R';
			break;
		case pieces::pawn1:
		case pieces::pawn2:
		case pieces::pawn3:
		case pieces::pawn4:
		case pieces::pawn5:
		case pieces::pawn6:
		case pieces::pawn7:
		case pieces::pawn8:
			if( p.pieces[c][source].special ) {
				switch( p.promotions[c] >> (2 * (source - pieces::pawn1) ) & 0x03 ) {
					case promotions::queen:
						ret += 'Q';
						break;
					case promotions::rook:
						ret += 'R';
						break;
					case promotions::bishop:
						ret += 'B';
						break;
					case promotions::knight:
						ret += 'N';
						break;
				}
			}
			else {
				ret += ' ';
			}
			break;
		default:
			break;
	}

	ret += 'a' + p.pieces[c][source].column;
	ret += '1' + p.pieces[c][source].row;

	if( p.board[m.target_col][m.target_row] != pieces::nil ) {
		ret += 'x';
	}
	else if( source >= pieces::pawn1 && source <= pieces::pawn8
		&& p.pieces[c][source].column != m.target_col && !p.pieces[c][source].special )
	{
		// Must be en-passant
		ret += 'x';
	}
	else {
		ret += '-';
	}

	ret += 'a' + m.target_col;
	ret += '1' + m.target_row;

	if( source >= pieces::pawn1 && source <= pieces::pawn8 && !p.pieces[c][source].special ) {
		if( m.target_row == 0 || m.target_row == 7 ) {
			ret += "=Q";
		}
		else {
			ret += "  ";
		}
	}
	else {
		ret += "  ";
	}

	return ret;
}

void init_board( position& p )
{
	for( unsigned int c = 0; c <= 1; ++c ) {
		for( unsigned int i = 0; i < 8; ++i ) {
			p.pieces[c][pieces::pawn1 + i].column = i;
			p.pieces[c][pieces::pawn1 + i].row = (c == color::black) ? 6 : 1;

			p.pieces[c][pieces::pawn8 + 1 + i].row = (c == color::black) ? 7 : 0;
		}

		p.pieces[c][pieces::rook1].column = 0;
		p.pieces[c][pieces::rook2].column = 7;
		p.pieces[c][pieces::bishop1].column = 2;
		p.pieces[c][pieces::bishop2].column = 5;
		p.pieces[c][pieces::knight1].column = 1;
		p.pieces[c][pieces::knight2].column = 6;
		p.pieces[c][pieces::king].column = 4;
		p.pieces[c][pieces::queen].column = 3;
	}

	for( unsigned int x = 0; x < 8; ++x ) {
		for( unsigned int y = 0; y < 8; ++y ) {
			p.board[x][y] = pieces::nil;
		}
	}

	for( unsigned int c = 0; c <= 1; ++c ) {
		for( unsigned int i = 0; i < 16; ++i ) {
			p.pieces[c][i].alive = true;
			p.pieces[c][i].special = false;

			p.board[p.pieces[c][i].column][p.pieces[c][i].row] = i | (c << 4);
		}

		p.pieces[c][pieces::rook1].special = true;
		p.pieces[c][pieces::rook2].special = true;
	}

	p.promotions[0] = 0;
	p.promotions[1] = 0;

	p.can_en_passant = pieces::nil;

	p.material[0] = material_values::initial;
	p.material[1] = p.material[0];

	p.tropism[0] = evaluate_tropism_side( p, color::white );
	p.tropism[1] = evaluate_tropism_side( p, color::black );

	p.calc_pawn_map();
	p.evaluate_pawn_structure();
}

void init_board_from_pieces( position& p )
{
	for( unsigned int x = 0; x < 8; ++x ) {
		for( unsigned int y = 0; y < 8; ++y ) {
			p.board[x][y] = pieces::nil;
		}
	}

	for( unsigned int c = 0; c <= 1; ++c ) {
		for( unsigned int i = 0; i < 16; ++i ) {
			if( p.pieces[c][i].alive ) {
				p.board[p.pieces[c][i].column][p.pieces[c][i].row] = i | (c << 4);
			}
		}
	}
}

bool apply_move( position& p, move const& m, color::type c, bool& capture )
{
	unsigned char source = p.board[m.source_col][m.source_row];
	if( source == pieces::nil ) {
		// Happens in case of hash collisions. Sad but unavoidable.
		//std::cerr << "FAIL, moving dead piece!" << (int)source << " " << (int)m.target_col << " " << (int)m.target_row << std::endl;
		capture = false;
		return false;
	}
	source &= 0x0f;
	piece& pp = p.pieces[c][source];
	ASSERT( pp.alive );

	p.board[m.source_col][m.source_row] = pieces::nil;

	// Handle castling
	if( source == pieces::king ) {
		if( m.source_col == 4 ) {
			if( m.target_col == 6 ) {
				// Kingside
				ASSERT( !pp.special );
				ASSERT( p.pieces[c][pieces::rook2].special );
				ASSERT( p.pieces[c][pieces::rook2].alive );
				pp.special = 1;
				pp.column = 6;
				p.board[6][m.target_row] = pieces::king | (c << 4);
				p.board[7][m.target_row] = pieces::nil;
				p.board[5][m.target_row] = pieces::rook2 | (c << 4);
				p.pieces[c][pieces::rook2].column = 5;
				p.pieces[c][pieces::rook1].special = 0;
				p.pieces[c][pieces::rook2].special = 0;
				p.can_en_passant = pieces::nil;
				capture = false;
				p.tropism[0] = evaluate_tropism_side( p, color::white );
				p.tropism[1] = evaluate_tropism_side( p, color::black );
				return true;
			}
			else if( m.target_col == 2 ) {
				// Queenside
				pp.special = 1;
				pp.column = 2;
				p.board[2][m.target_row] = pieces::king | (c << 4);
				p.board[0][m.target_row] = pieces::nil;
				p.board[3][m.target_row] = pieces::rook1 | (c << 4);
				p.pieces[c][pieces::rook1].column = 3;
				p.pieces[c][pieces::rook1].special = 0;
				p.pieces[c][pieces::rook2].special = 0;
				p.can_en_passant = pieces::nil;
				capture = false;
				p.tropism[0] = evaluate_tropism_side( p, color::white );
				p.tropism[1] = evaluate_tropism_side( p, color::black );
				return true;
			}
		}
		p.pieces[c][pieces::rook1].special = 0;
		p.pieces[c][pieces::rook2].special = 0;
		p.tropism[c] -= evaluate_tropism_piece( p, c, source, m.source_col, m.source_row );
		p.tropism[c] += evaluate_tropism_piece( p, c, source, m.target_col, m.target_row );
	}
	else if( source == pieces::rook1 ) {
		pp.special = 0;
		p.tropism[c] -= evaluate_tropism_piece( p, c, source, m.source_col, m.source_row );
		p.tropism[c] += evaluate_tropism_piece( p, c, source, m.target_col, m.target_row );
	}
	else if( source == pieces::rook2 ) {
		pp.special = 0;
		p.tropism[c] -= evaluate_tropism_piece( p, c, source, m.source_col, m.source_row );
		p.tropism[c] += evaluate_tropism_piece( p, c, source, m.target_col, m.target_row );
	}
	else {
		p.tropism[c] -= evaluate_tropism_piece( p, c, source, m.source_col, m.source_row );
		p.tropism[c] += evaluate_tropism_piece( p, c, source, m.target_col, m.target_row );
	}

	bool const is_pawn = source >= pieces::pawn1 && source <= pieces::pawn8 && !pp.special;

	bool pawn_capture = false;

	unsigned char old_piece = p.board[m.target_col][m.target_row];
	if( old_piece != pieces::nil ) {
		// Ordinary capture
		ASSERT( (old_piece >> 4) != c );
		old_piece &= 0x0f;
		ASSERT( old_piece != pieces::king );

		piece& cp = p.pieces[1-c][old_piece];

		if( old_piece >= pieces::pawn1 && old_piece <= pieces::pawn8 ) {
			if( !p.pieces[1-c][old_piece].special ) {
				p.pawns.map[1-c] &= ~(1ull << (m.target_row * 8 + m.target_col));
				p.pawns.hash ^= get_pawn_structure_hash( static_cast<color::type>(1-c), m.target_col, m.target_row );
				pawn_capture = true;
				p.material[1-c] -= material_values::pawn;
			}
			else {
				unsigned short promoted = (p.promotions[1-c] >> (2 * (old_piece - pieces::pawn1) ) ) & 0x03;
				switch( promoted ) {
				case promotions::queen:
					p.material[1-c] -= material_values::queen;
					break;
				case promotions::rook:
					p.material[1-c] -= material_values::rook;
					break;
				case promotions::bishop:
					p.material[1-c] -= material_values::bishop;
					break;
				case promotions::knight:
					p.material[1-c] -= material_values::knight;
					break;
				}
			}
		}

		cp.alive = false;
		cp.special = false;

		p.tropism[1-c] -= evaluate_tropism_piece( p, static_cast<color::type>(1-c), source, m.target_col, m.target_row );

		capture = true;
	}
	else if( is_pawn && p.pieces[c][source].column != m.target_col ) {
		// Must be en-passant
		old_piece = p.board[m.target_col][m.source_row];
		ASSERT( (old_piece >> 4) != c );
		old_piece &= 0x0f;
		ASSERT( (p.can_en_passant & 0x0f) == old_piece );
		ASSERT( old_piece >= pieces::pawn1 && old_piece <= pieces::pawn8 );
		ASSERT( old_piece != pieces::king );
		piece& cp = p.pieces[1-c][old_piece];

		cp.alive = false;
		cp.special = false;
		p.board[m.target_col][m.source_row] = pieces::nil;

		p.pawns.map[1-c] &= ~(1ull << (m.source_row * 8 + m.target_col));
		p.pawns.hash ^= get_pawn_structure_hash( static_cast<color::type>(1-c), m.target_col, m.source_row );
		pawn_capture = true;
		p.material[1-c] -= material_values::pawn;

		p.tropism[1-c] -= evaluate_tropism_piece( p, static_cast<color::type>(1-c), source, m.target_col, m.source_row );

		capture = true;
	}
	else {
		capture = false;
	}

	if( is_pawn ) {
		p.pawns.map[c] &= ~(1ull << (m.source_row * 8 + m.source_col));
		p.pawns.hash ^= get_pawn_structure_hash( c, m.source_col, m.source_row );
		if( m.target_row == 0 || m.target_row == 7) {
			pp.special = true;
			p.promotions[c] |= promotions::queen << (2 * (source - pieces::pawn1) );
			p.can_en_passant = pieces::nil;
			p.material[c] -= material_values::pawn;
			p.material[c] += material_values::queen;
		}
		else if( (c == color::white) ? (pp.row + 2 == m.target_row) : (m.target_row + 2 == pp.row) ) {
			p.can_en_passant = source;
			if( c ) {
				p.can_en_passant |= 0x10;
			}
			p.pawns.map[c] |= 1ull << (m.target_row * 8 + m.target_col);
			p.pawns.hash ^= get_pawn_structure_hash( c, m.target_col, m.target_row );
		}
		else {
			p.can_en_passant = pieces::nil;
			p.pawns.map[c] |= 1ull << (m.target_row * 8 + m.target_col);
			p.pawns.hash ^= get_pawn_structure_hash( c, m.target_col, m.target_row );
		}
	}
	else {
		p.can_en_passant = pieces::nil;
	}

	p.board[m.target_col][m.target_row] = source | (c << 4);

	pp.column = m.target_col;
	pp.row = m.target_row;

	if( is_pawn || pawn_capture ) {
		short pawn_eval;
		if( !pawn_hash_table.lookup( p.pawns.hash, pawn_eval ) ) {
			pawn_eval = evaluate_pawns( p.pawns.map, color::white) - evaluate_pawns( p.pawns.map, color::black );
			pawn_hash_table.store( p.pawns.hash, pawn_eval );
		}
		p.pawns.eval = pawn_eval;
	}

	if( source == pieces::king ) {
		p.tropism[1-c] = evaluate_tropism_side( p, static_cast<color::type>(1-c) );
	}

	return true;
}


bool apply_move( position& p, move_info const& mi, color::type c, bool& capture )
{
	move const& m = mi.m;
	unsigned char source = p.board[m.source_col][m.source_row];
	if( source == pieces::nil ) {
		// Happens in case of hash collisions. Sad but unavoidable.
		//std::cerr << "FAIL, moving dead piece!" << (int)source << " " << (int)m.target_col << " " << (int)m.target_row << std::endl;
		capture = false;
		return false;
	}
	source &= 0x0f;
	piece& pp = p.pieces[c][source];

	p.board[m.source_col][m.source_row] = pieces::nil;
	p.pawns = mi.pawns;

	// Handle castling
	if( source == pieces::king ) {
		if( m.source_col == 4 ) {
			if( m.target_col == 6 ) {
				// Kingside
				ASSERT( !pp.special );
				ASSERT( p.pieces[c][pieces::rook2].special );
				ASSERT( p.pieces[c][pieces::rook2].alive );
				pp.special = 1;
				pp.column = 6;
				p.board[6][m.target_row] = pieces::king | (c << 4);
				p.board[7][m.target_row] = pieces::nil;
				p.board[5][m.target_row] = pieces::rook2 | (c << 4);
				p.pieces[c][pieces::rook2].column = 5;
				p.pieces[c][pieces::rook1].special = 0;
				p.pieces[c][pieces::rook2].special = 0;
				p.can_en_passant = pieces::nil;
				capture = false;
				p.tropism[0] = evaluate_tropism_side( p, color::white );
				p.tropism[1] = evaluate_tropism_side( p, color::black );
				return true;
			}
			else if( m.target_col == 2 ) {
				// Queenside
				pp.special = 1;
				pp.column = 2;
				p.board[2][m.target_row] = pieces::king | (c << 4);
				p.board[0][m.target_row] = pieces::nil;
				p.board[3][m.target_row] = pieces::rook1 | (c << 4);
				p.pieces[c][pieces::rook1].column = 3;
				p.pieces[c][pieces::rook1].special = 0;
				p.pieces[c][pieces::rook2].special = 0;
				p.can_en_passant = pieces::nil;
				capture = false;
				p.tropism[0] = evaluate_tropism_side( p, color::white );
				p.tropism[1] = evaluate_tropism_side( p, color::black );
				return true;
			}
		}
		p.pieces[c][pieces::rook1].special = 0;
		p.pieces[c][pieces::rook2].special = 0;
	}
	else if( source == pieces::rook1 ) {
		pp.special = 0;
		p.tropism[c] -= evaluate_tropism_piece( p, c, source, m.source_col, m.source_row );
		p.tropism[c] += evaluate_tropism_piece( p, c, source, m.target_col, m.target_row );
	}
	else if( source == pieces::rook2 ) {
		pp.special = 0;
		p.tropism[c] -= evaluate_tropism_piece( p, c, source, m.source_col, m.source_row );
		p.tropism[c] += evaluate_tropism_piece( p, c, source, m.target_col, m.target_row );
	}
	else {
		p.tropism[c] -= evaluate_tropism_piece( p, c, source, m.source_col, m.source_row );
		p.tropism[c] += evaluate_tropism_piece( p, c, source, m.target_col, m.target_row );
	}

	bool const is_pawn = source >= pieces::pawn1 && source <= pieces::pawn8 && !pp.special;

	unsigned char old_piece = p.board[m.target_col][m.target_row];
	if( old_piece != pieces::nil ) {
		// Ordinary capture
		ASSERT( (old_piece >> 4) != c );
		old_piece &= 0x0f;
		ASSERT( old_piece != pieces::king );

		p.material[1-c] -= get_material_value( p, static_cast<color::type>(1-c), old_piece );

		piece& cp = p.pieces[1-c][old_piece];

		cp.alive = false;
		cp.special = false;

		p.tropism[1-c] -= evaluate_tropism_piece( p, static_cast<color::type>(1-c), source, m.target_col, m.target_row );

		capture = true;
	}
	else if( is_pawn && p.pieces[c][source].column != m.target_col ) {
		// Must be en-passant
		old_piece = p.board[m.target_col][m.source_row];
		ASSERT( (old_piece >> 4) != c );
		old_piece &= 0x0f;
		ASSERT( (p.can_en_passant & 0x0f) == old_piece );
		ASSERT( old_piece >= pieces::pawn1 && old_piece <= pieces::pawn8 );
		ASSERT( old_piece != pieces::king );
		piece& cp = p.pieces[1-c][old_piece];

		cp.alive = false;
		cp.special = false;
		p.board[m.target_col][m.source_row] = pieces::nil;

		p.material[1-c] -= material_values::pawn;

		p.tropism[1-c] -= evaluate_tropism_piece( p, static_cast<color::type>(1-c), source, m.target_col, m.source_row );

		capture = true;
	}
	else {
		capture = false;
	}

	if( is_pawn ) {
		if( m.target_row == 0 || m.target_row == 7) {
			pp.special = true;
			p.promotions[c] |= promotions::queen << (2 * (source - pieces::pawn1) );
			p.can_en_passant = pieces::nil;
			p.material[c] -= material_values::pawn;
			p.material[c] += material_values::queen;
		}
		else if( (c == color::white) ? (pp.row + 2 == m.target_row) : (m.target_row + 2 == pp.row) ) {
			p.can_en_passant = source;
			if( c ) {
				p.can_en_passant |= 0x10;
			}
		}
		else {
			p.can_en_passant = pieces::nil;
		}
	}
	else {
		p.can_en_passant = pieces::nil;
	}

	p.board[m.target_col][m.target_row] = source | (c << 4);

	pp.column = m.target_col;
	pp.row = m.target_row;

	if( source == pieces::king ) {
		p.tropism[1-c] = evaluate_tropism_side( p, static_cast<color::type>(1-c) );
	}

	return true;
}


namespace {
static mutex m;
static unsigned int random_unsigned_long_long_pos = 0;
static unsigned int random_unsigned_char = 0;
}

void init_random( int seed )
{
	random_unsigned_char = seed;
	random_unsigned_long_long_pos = (seed + 0xf00) % sizeof(precomputed_random_data);
}


unsigned char get_random_unsigned_char()
{
	scoped_lock l( m ) ;
	if( ++random_unsigned_char >= sizeof(precomputed_random_data) ) {
		random_unsigned_char = 0;
	}
	return precomputed_random_data[random_unsigned_char];
}

unsigned long long get_random_unsigned_long_long() {
	scoped_lock l( m ) ;
	random_unsigned_long_long_pos += sizeof( unsigned long long );

	if( random_unsigned_long_long_pos >= (sizeof(precomputed_random_data) - 8) ) {
		random_unsigned_long_long_pos = 0;
	}

	return *reinterpret_cast<unsigned long long*>(precomputed_random_data + random_unsigned_long_long_pos);
}

void position::calc_pawn_map()
{
	pawns.map[0] = 0;
	pawns.map[1] = 0;
	pawns.hash = 0;
	for( int c = 0; c < 2; ++c ) {
		for( int i = pieces::pawn1; i <= pieces::pawn8; ++i ) {
			piece const& pp = pieces[c][i];
			if( pp.alive && !pp.special ) {
				pawns.map[c] |= 1ull << (pp.row * 8 + pp.column);
				pawns.hash ^= get_pawn_structure_hash( static_cast<color::type>(c), pp.column, pp.row );
			}
		}
	}
}


void position::evaluate_pawn_structure()
{
	pawns.eval = evaluate_pawns( pawns.map, color::white ) - evaluate_pawns( pawns.map, color::black );
}

extern unsigned long long pawn_control[2][64];

void get_bitboards( position const& p, bitboard *bitboards )
{
	memset( bitboards, 0, sizeof(bitboard) * 2 );

	for( int c = 0; c < 2; ++c ) {
		for( int i = pieces::pawn1; i <= pieces::pawn8; ++i ) {
			piece const& pp = p.pieces[c][i];
			if( !pp.alive ) {
				continue;
			}
			if( pp.special ) {
				unsigned short promoted = (p.promotions[1-c] >> (2 * (i - pieces::pawn1) ) ) & 0x03;
				if( promoted == promotions::queen ) {
					bitboards[c].queens |= 1ull << (pp.column + pp.row * 8);
				}
				else if( promoted == promotions::bishop ) {
					bitboards[c].bishops |= 1ull << (pp.column + pp.row * 8);
				}
				else if( promoted == promotions::rook ) {
					bitboards[c].rooks |= 1ull << (pp.column + pp.row * 8);
				}
				else if( promoted == promotions::knight ) {
					bitboards[c].knights |= 1ull << (pp.column + pp.row * 8);
				}
			}
			else {
				bitboards[c].pawns |= 1ull << (pp.column + pp.row * 8);
				bitboards[c].pawn_control |= pawn_control[c][pp.column + pp.row * 8];
			}
		}


		{
			piece const& pp = p.pieces[c][pieces::rook1];
			if( pp.alive ) {
				bitboards[c].rooks |= 1ull << (pp.column + pp.row * 8);
			}
		}

		{
			piece const& pp = p.pieces[c][pieces::rook2];
			if( pp.alive ) {
				bitboards[c].rooks |= 1ull << (pp.column + pp.row * 8);
			}
		}

		{
			piece const& pp = p.pieces[c][pieces::knight1];
			if( pp.alive ) {
				bitboards[c].knights |= 1ull << (pp.column + pp.row * 8);
			}
		}

		{
			piece const& pp = p.pieces[c][pieces::knight2];
			if( pp.alive ) {
				bitboards[c].knights |= 1ull << (pp.column + pp.row * 8);
			}
		}

		{
			piece const& pp = p.pieces[c][pieces::bishop1];
			if( pp.alive ) {
				bitboards[c].bishops |= 1ull << (pp.column + pp.row * 8);
			}
		}

		{
			piece const& pp = p.pieces[c][pieces::bishop1];
			if( pp.alive ) {
				bitboards[c].bishops |= 1ull << (pp.column + pp.row * 8);
			}
		}

		{
			piece const& pp = p.pieces[c][pieces::queen];
			if( pp.alive ) {
				bitboards[c].queens |= 1ull << (pp.column + pp.row * 8);
			}
		}

		{
			piece const& pp = p.pieces[c][pieces::king];
			bitboards[c].king |= 1ull << (pp.column + pp.row * 8);
		}

		bitboards[c].all_pieces = bitboards[c].pawns | bitboards[c].knights | bitboards[c].bishops | bitboards[c].rooks | bitboards[c].queens | bitboards[c].king;
	}
}
