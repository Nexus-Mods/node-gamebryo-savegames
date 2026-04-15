const cp = require('child_process');
const fs = require('fs');
const path = require('path');

const ZLIB_VERSION = '1.3.1';
const ZLIB_TAG = `v${ZLIB_VERSION}`;
const ZLIB_REPO = 'https://github.com/madler/zlib.git';
const SRC_DIR = `zlib-${ZLIB_VERSION}`;
const INSTALL_DIR = 'zlib';

function run(cmd, args, cwd) {
  return new Promise((resolve, reject) => {
    const proc = cp.spawn(cmd, args, { cwd, shell: process.platform === 'win32' });
    proc.stdout.on('data', data => process.stdout.write(data));
    proc.stderr.on('data', data => process.stderr.write(data));
    proc.on('close', code => {
      console.log('finished', cmd, args.join(' '), code);
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(`${cmd} ${args.join(' ')} exited with code ${code}`));
      }
    });
  });
}

async function fetchSource() {
  try {
    fs.statSync(SRC_DIR);
    console.log(`${SRC_DIR} already present, skipping clone`);
    return;
  } catch (err) {
    // not present, clone below
  }
  console.log(`cloning ${ZLIB_REPO} @ ${ZLIB_TAG} into ${SRC_DIR}`);
  await run('git', ['clone', '--depth=1', '--branch', ZLIB_TAG, ZLIB_REPO, SRC_DIR]);
}

async function build() {
  const srcPath = path.join(__dirname, SRC_DIR);
  const buildPath = path.join(srcPath, 'build');
  const instPath = path.join(__dirname, INSTALL_DIR);
  console.log('build zlib', srcPath, buildPath, instPath);
  try {
    fs.mkdirSync(buildPath);
  } catch (err) {
    if (err.code !== 'EEXIST') throw err;
  }
  await run('cmake.exe', ['..', `-DCMAKE_INSTALL_PREFIX=${instPath}`, '-A', 'x64'], buildPath);
  await run('cmake.exe', ['--build', '.', '--config', 'Release'], buildPath);
  await run('cmake.exe', ['--install', '.'], buildPath);
}

async function main() {
  if (process.platform !== 'win32') {
    // On *nix the system package manager is expected to supply zlib
    // (see README). No-op here.
    return;
  }
  try {
    fs.statSync(INSTALL_DIR);
    console.log('zlib already installed');
    return;
  } catch (err) {
    // not installed, proceed
  }
  await fetchSource();
  await build();
}

main().catch(err => {
  console.error(err);
  process.exit(1);
});
