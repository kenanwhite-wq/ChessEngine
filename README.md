## SIMPLE CHESS ENGINE

uses chess.js and chessboard.js for the board and move logic, uses c++ for the engine bit and does standard evaluation and pruning to find the best move

## Building

Requires [Emscripten](https://emscripten.org/docs/getting_started/downloads.html).

```
cd engine
emcc engine.cpp -O3 -o ../web/engine.js -sEXPORTED_FUNCTIONS=_get_best_move -sEXPORTED_RUNTIME_METHODS=cwrap
```

Then serve the `web` folder (e.g. `python3 -m http.server`) and open http://localhost:8000.
