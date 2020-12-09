const cp = require('child_process');
const fs = require('fs');
const path = require('path');
const StreamZip = require('node-stream-zip');
const fetch = require('cross-fetch');
const tar = require('tar');

const TEMP_PATH = 'zlib.tar.gz';

function download(cb) {
    fetch('https://www.zlib.net/zlib-1.2.11.tar.gz')
        .then(res => {
            console.log(res.status, res.statusText);
            res.arrayBuffer()
                .then(buffer => {
                    fs.writeFile('zlib.tar.gz', Buffer.from(buffer), cb);
                })
        })
        .catch(err => {
            console.error('download failed', err);
        });
}

function run(cmd, args, cwd) {
    return new Promise((resolve, reject) => {
      const proc = cp.spawn(cmd, args, { cwd });
      proc.stdout.on('data', data => console.log(data.toString()));
      proc.stderr.on('data', data => console.error(data.toString()));
      proc.on('close', code => {
        console.log('finished', cmd, args.join(' '), code);
        if (code === 0) {
          resolve();
        } else {
          reject(new Error(`failed to run ${cmd}`));
        }
      });
    });
}

function unpack(cb) {
  const srcPath = path.join(__dirname, 'zlib-1.2.11');
  const buildPath = path.join(srcPath, 'build');
  const instPath = path.join(__dirname, 'zlib');
  console.log('unpack', srcPath, buildPath, instPath);
  tar.extract({ file: TEMP_PATH })
  .then(() => fs.promises.mkdir(buildPath).catch(err => console.log(err.message)))
  .then(() => run('cmake.exe', ['..', `-DCMAKE_INSTALL_PREFIX=${instPath}`], buildPath))
  .then(() => run('cmake.exe', ['--build', '.', '--config', 'Release'], buildPath))
  .then(() => run('cmake.exe', ['--install', '.'], buildPath))
  .then(cb)
}

try {
    fs.statSync('zlib');
    console.log('zlib already installed');
} catch(err) {
    download(() => {
        unpack(() => {
        });
    });
}
