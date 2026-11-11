// Fails if any source file in this repository, as it is on disk now (untracked
// files included), scores below 9.
import { spawnSync } from 'node:child_process';
import { accessSync, constants, copyFileSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const minimumScore = '9';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');
process.chdir(ROOT);

function isExecutable(path) {
  try {
    accessSync(path, constants.X_OK);
    return true;
  } catch {
    return false;
  }
}

// Runs a command the way `set -e` would: on failure, exit with its status.
function run(command, args, options = {}) {
  const standardOutput = options.capture ? 'pipe' : 'inherit';
  const result = spawnSync(command, args, {
    stdio: ['inherit', standardOutput, 'inherit'],
    encoding: 'utf8',
    env: options.env ?? process.env,
  });

  if (result.error)
    throw result.error;

  if (result.status !== 0)
    process.exit(result.status ?? 1);

  return result.stdout;
}

let binary = 'build/codehealth';

if (isExecutable('build/codehealth.exe'))
  binary = 'build/codehealth.exe';

const indexDirectory = mkdtempSync(join(tmpdir(), 'codehealth-self-check-'));
process.on('exit', () => rmSync(indexDirectory, { recursive: true, force: true }));

const index = join(indexDirectory, 'index');
copyFileSync(run('git', ['rev-parse', '--git-path', 'index'], { capture: true }).trim(), index);

const environmentWithTemporaryIndex = { ...process.env, GIT_INDEX_FILE: index };
run('git', ['add', '-A'], { env: environmentWithTemporaryIndex });
const tree = run('git', ['write-tree'], { env: environmentWithTemporaryIndex, capture: true }).trim();

run(join(ROOT, binary), ['.', '--ref', tree, '--check', minimumScore]);
