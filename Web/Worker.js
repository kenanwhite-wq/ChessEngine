let getBestMove;
var Module = {
    onRuntimeInitialized: function () {
        getBestMove = Module.cwrap('get_best_move', 'string', ['string', 'number']);
        postMessage({ type: 'ready' });
    }
};

importScripts('engine.js');

onmessage = function (event) {
    const request = event.data;
    try {
        if (typeof getBestMove !== 'function') {
            throw new Error('Chess engine is not ready yet.');
        }
        const move = getBestMove(request.fen, request.depth);
        postMessage({ type: 'move', id: request.id, move: move, fen: request.fen });
    } catch (error) {
        postMessage({
            type: 'error',
            id: request.id,
            message: error.message,
            fen: request.fen
        });
    }
};

onerror = function (event) {
    postMessage({ type: 'error', message: event.message || 'Failed to load the chess engine.' });
};
