#include <iostream>
#include <string>
#include <cstdint>
#include <sstream>
#include <vector>
#include <algorithm>
#include <cstdlib>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#else
#define EMSCRIPTEN_KEEPALIVE
#endif

enum PieceType {
    WHITE_PAWN, WHITE_KNIGHT, WHITE_BISHOP, WHITE_ROOK, WHITE_QUEEN, WHITE_KING,
    BLACK_PAWN, BLACK_KNIGHT, BLACK_BISHOP, BLACK_ROOK, BLACK_QUEEN, BLACK_KING
};

struct PieceBoard {
    uint64_t pieces[12]{};
    bool whiteToMove = true;
    uint8_t castlingRights = 0;
    int enPassantSquare = -1;
    int halfmoveClock = 0;
    int fullmoveNumber = 1;
};

using Board = PieceBoard;

struct Move {
    int from;            // square 0–63
    int to;              // square 0–63
    int promotion = -1;  // piece type to promote to, or -1 for none
};

constexpr int PIECE_VALUE[6] = {100, 320, 330, 500, 900, 20000};
constexpr int ORDER_VALUE[6] = {100, 320, 330, 500, 900, 20000};
static_assert(PIECE_VALUE[2] > PIECE_VALUE[1], "Bishops must be valued above knights");
static_assert(PIECE_VALUE[1] > 3 * PIECE_VALUE[0], "Knights must be valued above three pawns");
static_assert(PIECE_VALUE[2] > 3 * PIECE_VALUE[0], "Bishops must be valued above three pawns");
static_assert(PIECE_VALUE[1] + PIECE_VALUE[2] == PIECE_VALUE[3] + 3 * PIECE_VALUE[0] / 2,
              "Two minors must equal a rook and one-and-a-half pawns");
static_assert(PIECE_VALUE[4] + PIECE_VALUE[0] == 2 * PIECE_VALUE[3],
              "A queen and pawn must equal two rooks");
constexpr int INF = 1000000;
constexpr int MATE = 100000;

PieceBoard fromFEN(const char* fen) {
    PieceBoard board;
    if (fen == nullptr) return board;

    std::istringstream input(fen);
    std::string placement;
    std::string side;
    std::string castling;
    std::string enPassant;
    if (!(input >> placement >> side >> castling >> enPassant
                 >> board.halfmoveClock >> board.fullmoveNumber)) {
        return board;
    }

    int rank = 7;
    int file = 0;
    for (char symbol : placement) {
        if (symbol == '/') {
            if (file != 8 || rank == 0) return PieceBoard{};
            --rank;
            file = 0;
            continue;
        }
        if (symbol >= '1' && symbol <= '8') {
            file += symbol - '0';
            if (file > 8) return PieceBoard{};
            continue;
        }

        int piece = -1;
        switch (symbol) {
            case 'P': piece = WHITE_PAWN; break;
            case 'N': piece = WHITE_KNIGHT; break;
            case 'B': piece = WHITE_BISHOP; break;
            case 'R': piece = WHITE_ROOK; break;
            case 'Q': piece = WHITE_QUEEN; break;
            case 'K': piece = WHITE_KING; break;
            case 'p': piece = BLACK_PAWN; break;
            case 'n': piece = BLACK_KNIGHT; break;
            case 'b': piece = BLACK_BISHOP; break;
            case 'r': piece = BLACK_ROOK; break;
            case 'q': piece = BLACK_QUEEN; break;
            case 'k': piece = BLACK_KING; break;
            default: return PieceBoard{};
        }
        if (file >= 8) return PieceBoard{};
        board.pieces[piece] |= uint64_t{1} << (rank * 8 + file);
        ++file;
    }
    if (rank != 0 || file != 8) return PieceBoard{};

    board.whiteToMove = side == "w";
    if (side != "w" && side != "b") return PieceBoard{};
    if (castling != "-") {
        for (char right : castling) {
            switch (right) {
                case 'K': board.castlingRights |= 1; break;
                case 'Q': board.castlingRights |= 2; break;
                case 'k': board.castlingRights |= 4; break;
                case 'q': board.castlingRights |= 8; break;
                default: return PieceBoard{};
            }
        }
    }
    if (enPassant != "-" && enPassant.size() == 2 &&
        enPassant[0] >= 'a' && enPassant[0] <= 'h' &&
        enPassant[1] >= '1' && enPassant[1] <= '8') {
        board.enPassantSquare = (enPassant[1] - '1') * 8 + enPassant[0] - 'a';
    } else if (enPassant != "-") {
        return PieceBoard{};
    }
    return board;
}

int whatsAtSquare(const PieceBoard& board, int square) {
    if (square < 0 || square >= 64) return -1;
    for (int i = 0; i < 12; ++i) {
        if (board.pieces[i] & (1ULL << square)) {
            return i;  // Return the piece type index
        }
    }
    return -1;  // No piece at this square
}

bool kingInCheck(const PieceBoard& board, bool whiteKing);

std::vector<Move> findAllMoves(const PieceBoard& board) {
    std::vector<Move> moves;

    auto isFriendly = [&board](int piece) {
        if (piece == -1) return false;
        return board.whiteToMove
            ? piece <= WHITE_KING
            : piece >= BLACK_PAWN;
    };
    auto isEnemy = [&board](int piece) {
        if (piece == -1) return false;
        return board.whiteToMove
            ? piece >= BLACK_PAWN
            : piece <= WHITE_KING;
    };
    auto isEnemyKing = [&board](int piece) {
        return board.whiteToMove
            ? piece == BLACK_KING
            : piece == WHITE_KING;
    };
    auto addMove = [&board, &moves, &isFriendly, &isEnemyKing](int from, int to) {
        if (to < 0 || to >= 64) return;
        const int target = whatsAtSquare(board, to);
        if (!isFriendly(target) && !isEnemyKing(target)) {
            moves.push_back({from, to});
        }
    };
    auto addPawnMove = [&moves](int from, int to, bool white) {
        const int lastRank = white ? 7 : 0;
        if (to / 8 == lastRank) {
            const int promotions[] = {
                white ? WHITE_QUEEN : BLACK_QUEEN,
                white ? WHITE_ROOK : BLACK_ROOK,
                white ? WHITE_BISHOP : BLACK_BISHOP,
                white ? WHITE_KNIGHT : BLACK_KNIGHT
            };
            for (int promotion : promotions) {
                moves.push_back({from, to, promotion});
            }
        } else {
            moves.push_back({from, to});
        }
    };

    const int knightOffsets[8][2] = {
        {1, 2}, {2, 1}, {2, -1}, {1, -2},
        {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}
    };
    const int directions[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };

    for (int square = 0; square < 64; ++square) {
        int pieceType = whatsAtSquare(board, square);
        if (!isFriendly(pieceType)) continue;

        const int file = square % 8;
        const int rank = square / 8;
        const bool white = board.whiteToMove;

        if (pieceType == WHITE_PAWN || pieceType == BLACK_PAWN) {
            const int step = white ? 1 : -1;
            const int nextRank = rank + step;
            const int startingRank = white ? 1 : 6;
            if (nextRank < 0 || nextRank >= 8) continue;

            const int forward = nextRank * 8 + file;
            if (whatsAtSquare(board, forward) == -1) {
                addPawnMove(square, forward, white);
                const int doubleRank = rank + 2 * step;
                const int doubleForward = doubleRank * 8 + file;
                if (rank == startingRank &&
                    whatsAtSquare(board, doubleForward) == -1) {
                    moves.push_back({square, doubleForward});
                }
            }

            for (int fileStep : {-1, 1}) {
                const int targetFile = file + fileStep;
                if (targetFile < 0 || targetFile >= 8) continue;
                const int target = nextRank * 8 + targetFile;
                const int targetPiece = whatsAtSquare(board, target);
                if (isEnemy(targetPiece) && !isEnemyKing(targetPiece)) {
                    addPawnMove(square, target, white);
                } else if (target == board.enPassantSquare) {
                    moves.push_back({square, target});
                }
            }
        } else if (pieceType == WHITE_KNIGHT || pieceType == BLACK_KNIGHT) {
            for (const auto& offset : knightOffsets) {
                const int targetFile = file + offset[0];
                const int targetRank = rank + offset[1];
                if (targetFile < 0 || targetFile >= 8 ||
                    targetRank < 0 || targetRank >= 8) continue;
                addMove(square, targetRank * 8 + targetFile);
            }
        } else {
            const bool bishop = pieceType == WHITE_BISHOP || pieceType == BLACK_BISHOP;
            const bool rook = pieceType == WHITE_ROOK || pieceType == BLACK_ROOK;
            const bool queen = pieceType == WHITE_QUEEN || pieceType == BLACK_QUEEN;
            const bool king = pieceType == WHITE_KING || pieceType == BLACK_KING;
            if (!bishop && !rook && !queen && !king) continue;

            for (int direction = 0; direction < 8; ++direction) {
                const bool diagonal = direction >= 4;
                if ((bishop && !diagonal) || (rook && diagonal)) continue;

                int targetFile = file + directions[direction][0];
                int targetRank = rank + directions[direction][1];
                while (targetFile >= 0 && targetFile < 8 &&
                       targetRank >= 0 && targetRank < 8) {
                    const int target = targetRank * 8 + targetFile;
                    const int targetPiece = whatsAtSquare(board, target);
                    if (isFriendly(targetPiece)) break;
                    if (isEnemyKing(targetPiece)) break;
                    if (targetPiece != -1 && !isEnemy(targetPiece)) break;

                    moves.push_back({square, target});
                    if (targetPiece != -1 || king) break;
                    targetFile += directions[direction][0];
                    targetRank += directions[direction][1];
                }
            }

            if (king) {
                const int homeSquare = white ? 4 : 60;
                const int kingSideRight = white ? 1 : 4;
                const int queenSideRight = white ? 2 : 8;
                const int rookPiece = white ? WHITE_ROOK : BLACK_ROOK;

                if (square == homeSquare && !kingInCheck(board, white)) {
                    const int kingSideRook = homeSquare + 3;
                    if ((board.castlingRights & kingSideRight) != 0 &&
                        whatsAtSquare(board, homeSquare + 1) == -1 &&
                        whatsAtSquare(board, homeSquare + 2) == -1 &&
                        whatsAtSquare(board, kingSideRook) == rookPiece) {
                        PieceBoard transitBoard = board;
                        transitBoard.pieces[white ? WHITE_KING : BLACK_KING] &=
                            ~(uint64_t{1} << homeSquare);
                        transitBoard.pieces[white ? WHITE_KING : BLACK_KING] |=
                            uint64_t{1} << (homeSquare + 1);
                        if (!kingInCheck(transitBoard, white)) {
                            moves.push_back({homeSquare, homeSquare + 2});
                        }
                    }

                    const int queenSideRook = homeSquare - 4;
                    if ((board.castlingRights & queenSideRight) != 0 &&
                        whatsAtSquare(board, homeSquare - 1) == -1 &&
                        whatsAtSquare(board, homeSquare - 2) == -1 &&
                        whatsAtSquare(board, homeSquare - 3) == -1 &&
                        whatsAtSquare(board, queenSideRook) == rookPiece) {
                        PieceBoard transitBoard = board;
                        transitBoard.pieces[white ? WHITE_KING : BLACK_KING] &=
                            ~(uint64_t{1} << homeSquare);
                        transitBoard.pieces[white ? WHITE_KING : BLACK_KING] |=
                            uint64_t{1} << (homeSquare - 1);
                        if (!kingInCheck(transitBoard, white)) {
                            moves.push_back({homeSquare, homeSquare - 2});
                        }
                    }
                }
            }
        }
    }

    return moves;
}

PieceBoard MakeMove(const PieceBoard& board, const Move& move) {
    PieceBoard next = board;
    const int pieceType = whatsAtSquare(board, move.from);
    if (pieceType == -1 || move.to < 0 || move.to >= 64) return next;

    const uint64_t fromMask = uint64_t{1} << move.from;
    const uint64_t toMask = uint64_t{1} << move.to;
    const bool pawnMove = pieceType == WHITE_PAWN || pieceType == BLACK_PAWN;
    const int capturedSquare = pawnMove && move.to == board.enPassantSquare &&
        whatsAtSquare(board, move.to) == -1
            ? move.to + (board.whiteToMove ? -8 : 8)
            : move.to;
    const int capturedPiece = whatsAtSquare(board, capturedSquare);
    if (capturedPiece != -1) {
        next.pieces[capturedPiece] &= ~(uint64_t{1} << capturedSquare);
    }

    next.pieces[pieceType] &= ~fromMask;
    const int resultingPiece = move.promotion == -1 ? pieceType : move.promotion;
    next.pieces[resultingPiece] |= toMask;

    if ((pieceType == WHITE_KING || pieceType == BLACK_KING) &&
        std::abs(move.to - move.from) == 2) {
        const int rookPiece = pieceType == WHITE_KING ? WHITE_ROOK : BLACK_ROOK;
        const int rookFrom = move.to > move.from ? move.from + 3 : move.from - 4;
        const int rookTo = move.to > move.from ? move.from + 1 : move.from - 1;
        const uint64_t rookFromMask = uint64_t{1} << rookFrom;
        const uint64_t rookToMask = uint64_t{1} << rookTo;
        next.pieces[rookPiece] &= ~rookFromMask;
        next.pieces[rookPiece] |= rookToMask;
    }

    if (pieceType == WHITE_KING) next.castlingRights &= static_cast<uint8_t>(~3);
    if (pieceType == BLACK_KING) next.castlingRights &= static_cast<uint8_t>(~12);
    if (move.from == 0 || capturedSquare == 0) next.castlingRights &= static_cast<uint8_t>(~2);
    if (move.from == 7 || capturedSquare == 7) next.castlingRights &= static_cast<uint8_t>(~1);
    if (move.from == 56 || capturedSquare == 56) next.castlingRights &= static_cast<uint8_t>(~8);
    if (move.from == 63 || capturedSquare == 63) next.castlingRights &= static_cast<uint8_t>(~4);

    next.enPassantSquare = -1;
    if (pawnMove && std::abs(move.to - move.from) == 16) {
        next.enPassantSquare = (move.from + move.to) / 2;
    }
    next.halfmoveClock = pawnMove || capturedPiece != -1 ? 0 : board.halfmoveClock + 1;
    if (!board.whiteToMove) ++next.fullmoveNumber;
    next.whiteToMove = !board.whiteToMove;
    return next;
}

bool kingInCheck(const PieceBoard& board, bool whiteKing) {
    const int kingPiece = whiteKing ? WHITE_KING : BLACK_KING;
    int kingSquare = -1;
    for (int square = 0; square < 64; ++square) {
        if (whatsAtSquare(board, square) == kingPiece) {
            kingSquare = square;
            break;
        }
    }
    if (kingSquare == -1) return true;

    const int file = kingSquare % 8;
    const int rank = kingSquare / 8;
    const bool attackerIsWhite = !whiteKing;
    const int pawnRank = rank + (attackerIsWhite ? -1 : 1);
    for (int fileOffset : {-1, 1}) {
        const int pawnFile = file + fileOffset;
        if (pawnFile < 0 || pawnFile >= 8 || pawnRank < 0 || pawnRank >= 8) continue;
        const int attacker = whatsAtSquare(board, pawnRank * 8 + pawnFile);
        if (attacker == (attackerIsWhite ? WHITE_PAWN : BLACK_PAWN)) return true;
    }

    const int knightOffsets[8][2] = {
        {1, 2}, {2, 1}, {2, -1}, {1, -2},
        {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}
    };
    for (const auto& offset : knightOffsets) {
        const int targetFile = file + offset[0];
        const int targetRank = rank + offset[1];
        if (targetFile < 0 || targetFile >= 8 || targetRank < 0 || targetRank >= 8) continue;
        if (whatsAtSquare(board, targetRank * 8 + targetFile) ==
            (attackerIsWhite ? WHITE_KNIGHT : BLACK_KNIGHT)) return true;
    }

    const int directions[8][2] = {
        {1, 0}, {-1, 0}, {0, 1}, {0, -1},
        {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
    };
    for (int direction = 0; direction < 8; ++direction) {
        int targetFile = file + directions[direction][0];
        int targetRank = rank + directions[direction][1];
        int distance = 1;
        while (targetFile >= 0 && targetFile < 8 && targetRank >= 0 && targetRank < 8) {
            const int piece = whatsAtSquare(board, targetRank * 8 + targetFile);
            if (piece != -1) {
                if ((attackerIsWhite && piece <= WHITE_KING) ||
                    (!attackerIsWhite && piece >= BLACK_PAWN)) {
                    const bool diagonal = direction >= 4;
                    const bool slider = piece == (attackerIsWhite ? WHITE_QUEEN : BLACK_QUEEN) ||
                        (diagonal && piece == (attackerIsWhite ? WHITE_BISHOP : BLACK_BISHOP)) ||
                        (!diagonal && piece == (attackerIsWhite ? WHITE_ROOK : BLACK_ROOK));
                    const bool adjacentKing = distance == 1 &&
                        piece == (attackerIsWhite ? WHITE_KING : BLACK_KING);
                    if (slider || adjacentKing) return true;
                }
                break;
            }
            targetFile += directions[direction][0];
            targetRank += directions[direction][1];
            ++distance;
        }
    }
    return false;
}

std::vector<Move> filterMoves(const PieceBoard& board, const std::vector<Move>& moves) {
    std::vector<Move> legalMoves;

    for (const Move& move : moves) {
        const PieceBoard next = MakeMove(board, move);
        if (!kingInCheck(next, board.whiteToMove)) legalMoves.push_back(move);
    }

    return legalMoves;
}


std::vector<Move> generateLegalMoves(const PieceBoard& board) {
    std::vector<Move> allMoves = findAllMoves(board);
    return filterMoves(board, allMoves);
}


std::vector<int> Places(int PieceType, PieceBoard& board) {
    std::vector<int> places;
    for (int i = 0; i < 64; ++i) {
        if (whatsAtSquare(board, i) == PieceType) {
            places.push_back(i);
        }
    }
    return places;
}


int Population(const PieceBoard& board, int pieceType) {
    int count = 0;
    for (int square = 0; square < 64; ++square) {
        if (whatsAtSquare(board, square) == pieceType) {
            ++count;
        }
    }
    return count;
}

const int PAWN_TABLE[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};
 
const int KNIGHT_TABLE[64] = {
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50
};
 
const int BISHOP_TABLE[64] = {
   -20,-10,-10,-10,-10,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5, 10, 10,  5,  0,-10,
   -10,  5,  5, 10, 10,  5,  5,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10, 10, 10, 10, 10, 10, 10,-10,
   -10,  5,  0,  0,  0,  0,  5,-10,
   -20,-10,-10,-10,-10,-10,-10,-20
};
 
const int ROOK_TABLE[64] = {
     0,  0,  0,  0,  0,  0,  0,  0,
     5, 10, 10, 10, 10, 10, 10,  5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     0,  0,  0,  5,  5,  0,  0,  0
};
 
const int QUEEN_TABLE[64] = {
   -20,-10,-10, -5, -5,-10,-10,-20,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -10,  0,  5,  5,  5,  5,  0,-10,
    -5,  0,  5,  5,  5,  5,  0, -5,
     0,  0,  5,  5,  5,  5,  0, -5,
   -10,  5,  5,  5,  5,  5,  0,-10,
   -10,  0,  5,  0,  0,  0,  0,-10,
   -20,-10,-10, -5, -5,-10,-10,-20
};
 
const int KING_MIDDLE_TABLE[64] = {
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -10,-20,-20,-20,-20,-20,-20,-10,
    20, 20,  0,  0,  0,  0, 20, 20,
    20, 30, 10,  0,  0, 10, 30, 20
};
 
const int KING_END_TABLE[64] = {
   -50,-40,-30,-20,-20,-30,-40,-50,
   -30,-20,-10,  0,  0,-10,-20,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-30,  0,  0,  0,  0,-30,-30,
   -50,-30,-30,-30,-30,-30,-30,-50
};
 
bool isEndgame(const PieceBoard& b) {
    int whiteQueens = Population(b, WHITE_QUEEN);
    int blackQueens = Population(b, BLACK_QUEEN);
    int whiteRooks = Population(b, WHITE_ROOK);
    int blackRooks = Population(b, BLACK_ROOK);
    int whiteMinors = Population(b, WHITE_KNIGHT) + Population(b, WHITE_BISHOP);
    int blackMinors = Population(b, BLACK_KNIGHT) + Population(b, BLACK_BISHOP);
 
    if (whiteQueens == 0 && blackQueens == 0) return true;
 
    bool whiteOk = (whiteQueens == 0) || (whiteRooks == 0 && whiteMinors <= 1);
    bool blackOk = (blackQueens == 0) || (blackRooks == 0 && blackMinors <= 1);
    return whiteOk && blackOk;
}




int evaluate(PieceBoard b) {
    // Pick the king table depending on game phase.
    const int* kingTable = isEndgame(b) ? KING_END_TABLE : KING_MIDDLE_TABLE;
 
    // One table per piece type, in enum order.
    const int* tables[6] = {
        PAWN_TABLE, KNIGHT_TABLE, BISHOP_TABLE, ROOK_TABLE, QUEEN_TABLE, kingTable
    };
 
    int score = 0;   // from WHITE's point of view while we add things up
 
    for (int p = 0; p < 6; p++) {
        // White pieces: add value + table bonus (flipped lookup)
        uint64_t white = b.pieces[p];
        while (white) {
            int sq = __builtin_ctzll(white);
            score += PIECE_VALUE[p] + tables[p][sq ^ 56];
            white &= white - 1;
        }
 
        // Black pieces: subtract value + table bonus (direct lookup)
        uint64_t black = b.pieces[p + 6];
        while (black) {
            int sq = __builtin_ctzll(black);
            score -= PIECE_VALUE[p] + tables[p][sq];
            black &= black - 1;
        }
    }
    return b.whiteToMove ? score : -score;
}



bool isCapture(const Board& b, const Move& m) {
    if (whatsAtSquare(b, m.to) != -1) return true;
    // en passant: a pawn moving onto the en passant square
    const int movingPiece = whatsAtSquare(b, m.from);
    return m.to == b.enPassantSquare &&
        (movingPiece == WHITE_PAWN || movingPiece == BLACK_PAWN);
}



int moveOrderScore(const Board& b, const Move& m) {
    int score = 0;
    int victim = whatsAtSquare(b, m.to);
    if (victim != -1) {
        int attacker = whatsAtSquare(b, m.from);
        score += 100000 + 10 * ORDER_VALUE[victim % 6] - ORDER_VALUE[attacker % 6];
    }
    if (m.promotion != -1) score += 90000;
    return score;   // quiet moves stay at 0
}

void orderMoves(const PieceBoard& b, std::vector<Move>& moves) {
    // Sort highest score first. The [&](...) { ... } part is a "lambda":
    // a small unnamed function telling std::sort how to compare two moves.
    std::sort(moves.begin(), moves.end(), [&](const Move& x, const Move& y) {
        return moveOrderScore(b, x) > moveOrderScore(b, y);
    });
}

int quiescence(const Board& b, int alpha, int beta) {
    // "Stand pat": the side to move can usually choose not to capture,
    // so the current evaluation is a lower bound on its score.
    int standPat = evaluate(b);
    if (standPat >= beta) return beta;
    if (standPat > alpha) alpha = standPat;
 
    std::vector<Move> moves = generateLegalMoves(b);
    orderMoves(b, moves);
 
    for (const Move& m : moves) {
        if (!isCapture(b, m)) continue;
 
        Board next = MakeMove(b, m);
        int score = -quiescence(next, -beta, -alpha);
 
        if (score >= beta) return beta;
        if (score > alpha) alpha = score;
    }
    return alpha;
}

int search(const Board& b, int depth, int ply, int alpha, int beta) {
    std::vector<Move> moves = generateLegalMoves(b);
 
    // No legal moves: checkmate or stalemate.
    if (moves.empty()) {
        if (kingInCheck(b, b.whiteToMove)) return -MATE + ply;   // we're mated
        return 0;                                                // stalemate
    }
 
    if (depth == 0) return quiescence(b, alpha, beta);
 
    orderMoves(b, moves);
 
    for (const Move& m : moves) {
        Board next = MakeMove(b, m);
        int score = -search(next, depth - 1, ply + 1, -beta, -alpha);
 
        if (score >= beta) return beta;    // cutoff: opponent will avoid this line
        if (score > alpha) alpha = score;  // new best for us
    }
    return alpha;
}

Move findBestMove(const PieceBoard& b, int maxDepth) {
    std::vector<Move> moves = generateLegalMoves(b);
    if (moves.empty()) return Move{};   // game is over; the caller should check for this
 
    orderMoves(b, moves);
    Move best = moves[0];
 
    for (int depth = 1; depth <= maxDepth; depth++) {
        int alpha = -INF;
        int bestIndex = 0;
 
        for (int i = 0; i < (int)moves.size(); i++) {
            Board next = MakeMove(b, moves[i]);
            int score = -search(next, depth - 1, 1, -INF, -alpha);
            if (score > alpha) {
                alpha = score;
                bestIndex = i;
            }
        }
 
        best = moves[bestIndex];
        std::swap(moves[0], moves[bestIndex]);   // try it first next iteration
 
        if (alpha >= MATE - 1000) break;          // found a forced mate: no need to go deeper
    }
    return best;
}
 

std::string moveToUci(const Move& move) {
    if (move.from < 0 || move.from >= 64 || move.to < 0 || move.to >= 64) {
        return "0000";
    }
    std::string result;
    result += static_cast<char>('a' + move.from % 8);
    result += static_cast<char>('1' + move.from / 8);
    result += static_cast<char>('a' + move.to % 8);
    result += static_cast<char>('1' + move.to / 8);
    if (move.promotion != -1) {
        switch (move.promotion % 6) {
            case WHITE_KNIGHT: result += 'n'; break;
            case WHITE_BISHOP: result += 'b'; break;
            case WHITE_ROOK: result += 'r'; break;
            default: result += 'q'; break;
        }
    }
    return result;
}

extern "C" {
EMSCRIPTEN_KEEPALIVE
const char* get_best_move(const char* fen, int depth) {
    static std::string result;
    const PieceBoard board = fromFEN(fen);
    const Move move = findBestMove(board, std::max(1, depth));
    result = moveToUci(move);
    return result.c_str();
}
}

int main() {


    
    return 0;
}
