import express from 'express'
import http from 'http'
import path from 'path'
import { WebSocketServer } from 'ws'
import { fileURLToPath } from 'url'
import AudioId from './audioid.mjs'

const currentFolder = path.dirname(fileURLToPath(import.meta.url))

const app = express()
const server = http.Server(app)

const wss = new WebSocketServer({ server: server, path: '/audioid', clientTracking: true })

function broadcast(message) {
    for (const ws of wss.clients) {
        ws.send(message);
    }
}

const maxHistory = 50;
const recent = [];
function audioEventHandler(eventData) {
    const now = Date.now();

    const debugInfo = `AUDIOID: At ${eventData.time.toFixed(2) / 1000}, event '${eventData.type}', label '${eventData.label}', duration ${eventData.duration.toFixed(2)} s.`;
    console.log(debugInfo);
    
    const last = recent.length > 0 ? recent[recent.length - 1] : null;  // = recent.at(-1);
    const update = last && last.type == eventData.type && last.label == eventData.label;
    if (update) {
        // Just update duration
        last.duration = eventData.duration;
    } else {
        recent.push(eventData);
        recent[recent.length - 1].created = now;
    }
    recent[recent.length - 1].updated = now;
    recent.splice(0, recent.length - maxHistory);

    const output = {
        message: update ? 'update' : 'new',
        data: recent[recent.length - 1],
    };
    broadcast(JSON.stringify(output));
}

wss.on('connection', (ws) => {
    const output = {
        message: 'recent',
        data: recent,
    }
    ws.send(JSON.stringify(output));
    //ws.on('message', (message) => { ws.send(message); })
})

app.use('/', express.static(path.join(currentFolder, 'public')))

const audioId = new AudioId({
    binaryPath: '../audioid', 
    eventsFile: '../events.ini',
    stateFile: '../state.ini',
});
audioId.start(audioEventHandler);

const port = 3001;
server.listen(port, () => {
    console.log(`Listening at http://localhost:${port}`);
});
