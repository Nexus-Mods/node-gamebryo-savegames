Object.defineProperty(exports, "__esModule", { value: true });

let nbind = require('nbind');
let path = require('path');

function init(subdir) {
  let baseDir = __dirname.replace('app.asar' + path.sep, 'app.asar.unpacked' + path.sep);
  let modulePath = subdir !== undefined ? path.join(baseDir, subdir) : baseDir;
  return nbind.init(modulePath).lib;
}

module.exports.default = init;
