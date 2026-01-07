// fox_geese_server.cpp
#include "httplib.h"
#include "json.hpp"
#include <vector>
#include <array>
#include <mutex>
#include <optional>

using json = nlohmann::json;

// 0 — порожньо, 1 — гуска, 2 — лиса
struct Game {
    std::vector<int> board;          // 33 клітинки
    std::vector<std::pair<int,int>> coords; // індекс -> (x,y)
    int idx_map[7][7];               // (y,x) -> індекс або -1
    int current = 1;                 // 1 = хід гусей, 2 = хід лиси
    int eaten = 0;                   // скільки гусей з’їдено
    int winner = 0;                  // 0 – ще граємо, 1 – виграли гуси, 2 – виграла лиса
};

static const int MASK[7][7] = {
    {0,0,1,1,1,0,0},  // row 0
    {0,0,1,1,1,0,0},  // row 1
    {1,1,1,1,1,1,1},  // row 2
    {1,1,1,1,1,1,1},  // row 3
    {1,1,1,1,1,1,1},  // row 4  <-- тут нижня з 7 клітин
    {0,0,1,1,1,0,0},  // row 5
    {0,0,1,1,1,0,0},  // row 6
};

void init_game(Game &g) {
    g.board.assign(33, 0);
    g.coords.clear();
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            g.idx_map[y][x] = -1;
        }
    }

    // формуємо 33 точки
    int idx = 0;
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; x < 7; ++x) {
            if (MASK[y][x]) {
                g.idx_map[y][x] = idx;
                g.coords.push_back({x,y});
                idx++;
            }
        }
    }

    // розставляємо 13 гусей на "трьох нижніх горизонталях":
    // row4: всі 7
    // row5: x=2,3,4
    // row6: x=2,3,4
    for (int x = 0; x < 7; ++x) {
        if (MASK[4][x]) {
            int i = g.idx_map[4][x];
            g.board[i] = 1;
        }
    }
    for (int x = 2; x <= 4; ++x) {
        int i = g.idx_map[5][x];
        g.board[i] = 1;
        int j = g.idx_map[6][x];
        g.board[j] = 1;
    }

    // лиса в центрі (3,3) — це вільна клітинка
    int fox_idx = g.idx_map[3][3];
    g.board[fox_idx] = 2;

    g.current = 1;  // починають гуси
    g.eaten = 0;
    g.winner = 0;
}

bool cell_exists(const Game &g, int x, int y) {
    if (x < 0 || x > 6 || y < 0 || y > 6) return false;
    return MASK[y][x] != 0;
}

int index_of(const Game &g, int x, int y) {
    return g.idx_map[y][x];
}

std::vector<int> orth_neighbors(const Game &g, int idx) {
    auto [x,y] = g.coords[idx];
    std::vector<int> res;
    const int dx[4] = {1,-1,0,0};
    const int dy[4] = {0,0,1,-1};
    for (int k = 0; k < 4; ++k) {
        int nx = x + dx[k];
        int ny = y + dy[k];
        if (cell_exists(g, nx, ny)) {
            res.push_back(index_of(g, nx, ny));
        }
    }
    return res;
}

// можливі ходи лиси з урахуванням стрибків
std::vector<int> fox_simple_moves(const Game &g, int idx) {
    std::vector<int> moves;
    auto [x, y] = g.coords[idx];
    // 8 напрямків
    const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    for (int k = 0; k < 8; ++k) {
        int nx = x + dx[k];
        int ny = y + dy[k];
        if (cell_exists(g, nx, ny)) {
            int ni = index_of(g, nx, ny);
            if (g.board[ni] == 0) moves.push_back(ni);
        }
    }
    return moves;
}

struct Jump {
    int to;
    int captured;
};

std::vector<Jump> fox_jumps_from(const Game &g, int idx) {
    std::vector<Jump> jumps;
    auto [x, y] = g.coords[idx];
    // 8 напрямків
    const int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1};
    const int dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};

    for (int k = 0; k < 8; ++k) {
        int mx = x + dx[k];
        int my = y + dy[k];
        int jx = x + 2 * dx[k];
        int jy = y + 2 * dy[k];
        if (cell_exists(g, mx, my) && cell_exists(g, jx, jy)) {
            int mid = index_of(g, mx, my);
            int dst = index_of(g, jx, jy);
            if (g.board[mid] == 1 && g.board[dst] == 0) {
                jumps.push_back({dst, mid});
            }
        }
    }
    return jumps;
}

// перевірка, чи лиса взагалі має ходи
bool fox_has_any_move(const Game &g) {
    int fox = -1;
    for (int i = 0; i < (int)g.board.size(); ++i) {
        if (g.board[i] == 2) { fox = i; break; }
    }
    if (fox == -1) return false;
    if (!fox_simple_moves(g, fox).empty()) return true;
    if (!fox_jumps_from(g, fox).empty()) return true;
    return false;
}

int geese_count(const Game &g) {
    int c = 0;
    for (int v : g.board) if (v == 1) c++;
    return c;
}

int main() {
    Game game;
    init_game(game);
    std::mutex mtx;

    httplib::Server svr;

    // ---- GET /api/state ----
    svr.Get("/api/state", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        json j;
        j["board"] = game.board;
        j["current"] = game.current;
        j["winner"] = game.winner;
        j["geese_left"] = geese_count(game);
        res.set_content(j.dump(), "application/json");
    });

    // ---- GET /api/moves?pos=N ----
    // для підсвічування ходів на фронті
    svr.Get(R"(/api/moves)", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!req.has_param("pos")) {
            res.status = 400;
            res.set_content(R"({"error":"pos required"})", "application/json");
            return;
        }
        int pos = std::stoi(req.get_param_value("pos"));
        if (pos < 0 || pos >= (int)game.board.size()) {
            res.status = 400;
            res.set_content(R"({"error":"bad pos"})", "application/json");
            return;
        }
        json j;
        j["simple"] = json::array();
        j["jumps"] = json::array();

        if (game.board[pos] == 2) {
            // лиса
            for (int m : fox_simple_moves(game, pos)) j["simple"].push_back(m);
            for (auto jmp : fox_jumps_from(game, pos)) {
                j["jumps"].push_back({{"to", jmp.to}, {"captured", jmp.captured}});
            }
        } else if (game.board[pos] == 1) {
            // гуска — може на 1 клітинку по горизонталі/вертикалі
            for (int n : orth_neighbors(game, pos)) {
                if (game.board[n] == 0) j["simple"].push_back(n);
            }
        }
        res.set_content(j.dump(), "application/json");
    });

    // ---- POST /api/move ----
    // варіанти:
    // 1) { "from":10, "to":3 }  — звичайний хід
    // 2) { "sequence":[16,10,4] } — багатократний стрибок лиси
    svr.Post("/api/move", [&](const httplib::Request& req, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        if (game.winner != 0) {
            res.set_content(R"({"error":"game finished"})", "application/json");
            return;
        }

        json jr;
        try { jr = json::parse(req.body); }
        catch (...) {
            res.status = 400;
            res.set_content(R"({"error":"bad json"})", "application/json");
            return;
        }

        // багатократний стрибок
        if (jr.contains("sequence")) {
            auto seq = jr["sequence"];
            if (!seq.is_array() || seq.size() < 2) {
                res.status = 400;
                res.set_content(R"({"error":"bad sequence"})", "application/json");
                return;
            }
            std::vector<int> path = seq.get<std::vector<int>>();
            int from = path[0];
            if (from < 0 || from >= (int)game.board.size() || game.board[from] != 2) {
                res.status = 400;
                res.set_content(R"({"error":"fox only"})", "application/json");
                return;
            }
            if (game.current != 2) {
                res.status = 400;
                res.set_content(R"({"error":"not fox turn"})", "application/json");
                return;
            }

            int pos = from;
            int eaten_now = 0;
            for (size_t i = 1; i < path.size(); ++i) {
                int to = path[i];
                auto [x,y] = game.coords[pos];
                auto [tx,ty] = game.coords[to];
                int dx = tx - x;
                int dy = ty - y;
                if (!((dx==0 && (dy==2 || dy==-2)) || (dy==0 && (dx==2 || dx==-2)))) {
                    res.status = 400;
                    res.set_content(R"({"error":"invalid jump step"})", "application/json");
                    return;
                }
                int mx = x + (dx/2);
                int my = y + (dy/2);
                int mid = index_of(game, mx, my);
                if (game.board[mid] != 1 || game.board[to] != 0) {
                    res.status = 400;
                    res.set_content(R"({"error":"jump not possible"})", "application/json");
                    return;
                }
                // виконуємо стрибок
                game.board[mid] = 0;
                game.board[pos] = 0;
                game.board[to] = 2;
                pos = to;
                eaten_now++;
            }
            game.eaten += eaten_now;
            game.current = 1; // хід гусей

        } else {
            // одиночний хід
            int from = jr.value("from", -1);
            int to   = jr.value("to", -1);
            if (from < 0 || to < 0 || from >= (int)game.board.size() || to >= (int)game.board.size()) {
                res.status = 400;
                res.set_content(R"({"error":"bad indexes"})", "application/json");
                return;
            }

            int piece = game.board[from];
            if (piece == 0) {
                res.status = 400;
                res.set_content(R"({"error":"empty from"})", "application/json");
                return;
            }

            if (piece == 1 && game.current != 1) {
                res.status = 400;
                res.set_content(R"({"error":"not geese turn"})", "application/json");
                return;
            }
            if (piece == 2 && game.current != 2) {
                res.status = 400;
                res.set_content(R"({"error":"not fox turn"})", "application/json");
                return;
            }

            auto [fx,fy] = game.coords[from];
            auto [tx,ty] = game.coords[to];
            int dx = tx - fx;
            int dy = ty - fy;

            if (piece == 1) {
               // гуска: тільки вперед (зменшення Y) або вліво/вправо, але не назад
               if (!(((dx == 0 && dy == -1) || (dy == 0 && (dx == 1 || dx == -1))))) {
                   res.status = 400;
                   res.set_content(R"({"error":"geese move only forward/side"})", "application/json");
                   return;
               }
                if (game.board[to] != 0) {
                    res.status = 400;
                    res.set_content(R"({"error":"dest not empty"})", "application/json");
                    return;
                }
                game.board[from] = 0;
                game.board[to] = 1;
                game.current = 2; // тепер хід лиси
            } else if (piece == 2) {
                // лиса: або крок, або стрибок
                if ((abs(dx) <= 1 && abs(dy) <= 1) && !(dx == 0 && dy == 0)) {
                    // простий крок
                    if (game.board[to] != 0) {
                        res.status = 400;
                        res.set_content(R"({"error":"dest not empty"})", "application/json");
                        return;
                    }
                    game.board[from] = 0;
                    game.board[to] = 2;
                    game.current = 1;
                } else if ((abs(dx) == 2 && abs(dy) == 0) || (abs(dy) == 2 && abs(dx) == 0) || (abs(dx) == 2 && abs(dy) == 2)) {
                    // стрибок
                    int mx = fx + dx/2;
                    int my = fy + dy/2;
                    int mid = index_of(game, mx, my);
                    if (game.board[mid] != 1 || game.board[to] != 0) {
                        res.status = 400;
                        res.set_content(R"({"error":"jump not possible"})", "application/json");
                        return;
                    }
                    game.board[mid] = 0;
                    game.board[from] = 0;
                    game.board[to] = 2;
                    game.eaten += 1;
                    game.current = 1;
                } else {
                    res.status = 400;
                    res.set_content(R"({"error":"invalid fox move"})", "application/json");
                    return;
                }
            }
        }

        // перевірка завершення
        int geese_left = geese_count(game);
        if (13 - geese_left >= 8) {
            game.winner = 2; // лиса виграла
        } else if (!fox_has_any_move(game)) {
            game.winner = 1; // гуси виграли
        }

        json out;
        out["status"] = "ok";
        out["state"]["board"] = game.board;
        out["state"]["current"] = game.current;
        out["state"]["winner"] = game.winner;
        out["state"]["geese_left"] = geese_left;
        res.set_content(out.dump(), "application/json");
    });

    // щоб можна було перезапустити гру
    svr.Post("/api/reset", [&](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(mtx);
        init_game(game);
        res.set_content(R"({"status":"reset"})", "application/json");
    });

    svr.listen("0.0.0.0", 8080);
    return 0;
}
