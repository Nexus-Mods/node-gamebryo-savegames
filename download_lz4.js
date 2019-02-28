const fs = require('fs');
const StreamZip = require('node-stream-zip');
const fetch = require('cross-fetch');

const TEMP_PATH = 'lz4.zip';

function download(cb) {
    fetch('https://github.com/lz4/lz4/releases/download/v1.7.4.2/lz4_v1_7_4_win64.zip')
        .then(res => {
            console.log(res.status, res.statusText);
            res.arrayBuffer()
                .then(buffer => {
                    fs.writeFile('lz4.zip', Buffer.from(buffer), cb);
                })
        })
        .catch(err => {
            console.error('download failed', err);
        });
}

function unpack(cb) {
    const zip = new StreamZip({
        file: TEMP_PATH,
        storeEntries: true
    });

    zip.on('error', err => {
        console.error('unpack failed', err);
    })
        .on('ready', () => {
            fs.mkdirSync('lz4');
            zip.extract(null, './lz4', (err, count) => {
                console.log(err ? 'Extract error' : `Extracted ${count} entries`);
                zip.close();
                cb();
            });
        });
}

try {
    fs.statSync('lz4');
    console.log('lz4 already installed');
} catch(err) {
    download(() => {
        unpack(() => {
            fs.unlinkSync(TEMP_PATH);
        });
    });
}
