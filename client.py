# client.py
import tkinter as tk
import requests

API = "http://localhost:8080"

# та сама маска, що й у C++
MASK = [
    [0,0,1,1,1,0,0],
    [0,0,1,1,1,0,0],
    [1,1,1,1,1,1,1],
    [1,1,1,1,1,1,1],
    [1,1,1,1,1,1,1],
    [0,0,1,1,1,0,0],
    [0,0,1,1,1,0,0],
]

# зіставлення індекс -> (x,y)
coords = []
idx_map = [[-1]*7 for _ in range(7)]
idx = 0
for y in range(7):
    for x in range(7):
        if MASK[y][x]:
            coords.append((x,y))
            idx_map[y][x] = idx
            idx += 1

cell_size = 60
cols, rows = 7, 7
canvas_width = cols * cell_size + 200
canvas_height = rows * cell_size + 200
padding_x = (canvas_width - cols * cell_size) // 2
padding_y = (canvas_height - rows * cell_size) // 2

selected = None
possible_moves = []
possible_jumps = []  # [{to:..., captured:...}]
state = {}

def fetch_state():
    global state
    r = requests.get(f"{API}/api/state")
    state = r.json()

def fetch_moves(pos):
    r = requests.get(f"{API}/api/moves", params={"pos": pos})
    return r.json()

def board_to_screen(x, y):
    return padding_x + x * cell_size, padding_y + y * cell_size

def screen_to_board(px, py):
    # шукаємо найближчу активну точку
    best = None
    best_dist = 9999
    for i,(x,y) in enumerate(coords):
        sx, sy = board_to_screen(x,y)
        d = (sx - px)**2 + (sy - py)**2
        if d < best_dist:
            best_dist = d
            best = i
    if best_dist <= (cell_size//2)**2:
        return best
    return None

def draw():
    canvas.delete("all")
    # лінії
    for i,(x,y) in enumerate(coords):
        sx, sy = board_to_screen(x,y)
        # 4 напрями
        for dx,dy in [(1,0),(-1,0),(0,1),(0,-1)]:
            nx, ny = x+dx, y+dy
            if 0 <= nx < 7 and 0 <= ny < 7 and MASK[ny][nx]:
                ex, ey = board_to_screen(nx, ny)
                canvas.create_line(sx, sy, ex, ey, fill="#777")

    board = state.get("board", [])
    current = state.get("current", 0)
    winner = state.get("winner", 0)

    # підсвітка можливих
    for m in possible_moves:
        x,y = coords[m]
        sx, sy = board_to_screen(x,y)
        canvas.create_oval(sx-16, sy-16, sx+16, sy+16, outline="blue", width=2)
    for jmp in possible_jumps:
        x,y = coords[jmp["to"]]
        sx, sy = board_to_screen(x,y)
        canvas.create_oval(sx-16, sy-16, sx+16, sy+16, outline="purple", width=2)

    # фігури
    for i,(x,y) in enumerate(coords):
        sx, sy = board_to_screen(x,y)
        val = board[i] if i < len(board) else 0
        color = "white"
        if val == 1:
            color = "gold"
        elif val == 2:
            color = "red"
        if i == selected:
            canvas.create_oval(sx-20, sy-20, sx+20, sy+20, fill="#ccf", outline="black")
        canvas.create_oval(sx-14, sy-14, sx+14, sy+14, fill=color, outline="black")

    if winner == 1:
        canvas.create_text(270, 20, text="Гуси виграли", fill="green", font=("Arial", 14, "bold"))
    elif winner == 2:
        canvas.create_text(270, 20, text="Лиса виграла", fill="red", font=("Arial", 14, "bold"))
    else:
        turn_text = "Хід гусей" if current == 1 else "Хід лиси"
        canvas.create_text(270, 20, text=turn_text, fill="black", font=("Arial", 12))

def on_click(event):
    global selected, possible_moves, possible_jumps, state
    pos = screen_to_board(event.x, event.y)
    if pos is None:
        return
    board = state.get("board", [])
    current = state.get("current", 0)

    # якщо вже щось вибрано і ми клацаємо по ходові — надсилаємо
    if selected is not None and (pos in possible_moves or any(j["to"] == pos for j in possible_jumps)):
        # визначимо, чи це стрибок
        jump = next((j for j in possible_jumps if j["to"] == pos), None)
        if jump:
            # одиночний стрибок
            payload = {"from": selected, "to": pos}
        else:
            payload = {"from": selected, "to": pos}
        r = requests.post(f"{API}/api/move", json=payload)
        state = r.json().get("state", state)
        selected = None
        possible_moves = []
        possible_jumps = []
        fetch_state()
        draw()
        return

    # інакше — вибрати фігуру, свого кольору
    if pos < len(board) and board[pos] != 0:
        # перевіримо, що це фігура того, чия зараз черга
        if board[pos] == current:
            selected = pos
            moves = fetch_moves(pos)
            possible_moves = moves.get("simple", [])
            possible_jumps = moves.get("jumps", [])
        else:
            selected = None
            possible_moves = []
            possible_jumps = []
    else:
        selected = None
        possible_moves = []
        possible_jumps = []
    draw()

def reset():
    requests.post(f"{API}/api/reset")
    fetch_state()
    draw()

root = tk.Tk()
root.title("Лиса і гуси")
canvas = tk.Canvas(root, width=canvas_width, height=canvas_height, bg="white")
canvas.pack()
canvas.bind("<Button-1>", on_click)

btn_frame = tk.Frame(root)
btn_frame.pack(pady=5)
tk.Button(btn_frame, text="Оновити", command=lambda: (fetch_state(), draw())).pack(side=tk.LEFT, padx=5)
tk.Button(btn_frame, text="Нова гра", command=reset).pack(side=tk.LEFT, padx=5)

fetch_state()
draw()
root.mainloop()
