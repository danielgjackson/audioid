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

// wss.on('connection', (ws) => {
//     ws.send(lastState);
//     ws.on('message', (message) => { ws.send(message); })
// })

app.use('/', express.static(path.join(currentFolder, 'public')))

const audioId = new AudioId({
    binaryPath: '../audioid', 
    eventsFile: '../events.ini',
    stateFile: '../state.ini',
});
audioId.start((eventData) => {
    const debugInfo = `AUDIOID: At ${eventData.time.toFixed(2)} s, event '${eventData.type}', label '${eventData.label}', duration ${eventData.duration.toFixed(2)} s.`;
    console.log(debugInfo);
    broadcast(JSON.stringify(eventData));
});

server.listen(3000, () => {
    console.log('Listening at http://localhost:3000')
})
