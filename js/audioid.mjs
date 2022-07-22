import child_process from 'child_process'

// Utility function: run an external process and call a function for each line of its output
function captureExternalProcess(command, parameterArray, lineHandler) {
    // Received data buffer
    let received = '';

    // Start the child process
    console.log('Running external process...');
    const externalProcess = child_process.spawn(command, parameterArray);

    // Handle output from the child process
    externalProcess.stdout.on('data', (data) => {
        // Add received characters to input buffer
        received = received.concat(data);
        // Split off any full lines received
        for (; ;) {
            const lineEnd = received.indexOf('\n')
            if (lineEnd < 0) break;
            const line = received.slice(0, lineEnd).trim();
            received = received.slice(lineEnd + 1);
            lineHandler(line);
        }
    });

    // Handle child process terminating
    externalProcess.on('exit', (result) => {
        console.log('WARNING: External process exited: ' + result);
        externalProcess = null;
    });

    // Kill child process when we exit
    process.on('SIGINT', () => process.exit());
    process.on('SIGTERM', () => process.exit());
    process.on('exit', function () {
        if (externalProcess) {
            console.log('Stopping external process...');
            externalProcess.kill();
        }
    });
}


export class AudioId {
    constructor(options) {
        this.options = Object.assign({
            binaryPath: './audioid', 
            eventsFile: 'events.ini',
            stateFile: 'state.ini',
        }, options);
    }

    start(eventHandler) {
        // Run the external process, handle each received line
        captureExternalProcess(this.options.binaryPath, ['--events', this.options.eventsFile, '--state', this.options.stateFile], (line) => {
            const parts = line.split('\t');
            const event = {
                time: parseFloat(parts[0]),
                type: parts[1],
                label: parts[2],                    // 'grinder' / 'pump'
                duration: parseFloat(parts[3]),
            };
            //console.log(`AUDIOID: At ${event.time.toFixed(2)} s, event '${event.type}', label '${event.label}', duration ${event.duration.toFixed(2)} s.`);
            if (eventHandler != null) eventHandler(event);
        });
    }
}

export default AudioId;
