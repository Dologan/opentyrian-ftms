const { test } = require('node:test');
const assert = require('node:assert/strict');
const decide = require('./decide.cjs');
async function decision({ event = 'push', files = [{ filename: 'src/main.c' }],
  runs = [], artifacts = [], error = false, before = 'base' } = {}) {
  const outputs = {}, logs = [], failures = [], calls = [];
  const github = { rest: {
    repos: {
      getCommit: async () => ({ data: { sha: 'sha' } }),
      compareCommitsWithBasehead: async () => ({ data: { files } })
    }, actions: { listWorkflowRuns: 'runs', listWorkflowRunArtifacts: 'artifacts' }
  }, paginate: async (method, params) => {
    calls.push([method, params]);
    if (error) throw new Error('API unavailable');
    return method === 'runs' ? runs : (typeof artifacts === 'function' ? artifacts(params.run_id) : artifacts);
  } };
  await decide({ github, workflow: 'linux.yml', packages: ['package'],
    context: { repo: { owner: 'owner', repo: 'repo' }, sha: 'sha', runId: 10,
      eventName: event, payload: { before, release: { tag_name: 'tag' },
        ...(event === 'pull_request' ? { pull_request: {
          base: { sha: 'base' }, head: { sha: 'sha' }
        } } : {}) } },
    core: { setOutput: (key, value) => { outputs[key] = value; },
      info: message => logs.push(message), warning: message => logs.push(message),
      setFailed: message => failures.push(message) }
  });
  return { outputs, logs, failures, calls };
}
const success = { id: 11, head_sha: 'sha', event: 'push', conclusion: 'success',
  html_url: 'https://example.test/run/11' };
for (const event of ['push', 'pull_request']) {
  test(`${event}: docs-only skips; mixed files and code renames run`, async () => {
    assert.equal((await decision({ event, files: [
      { filename: 'README.md' }, { filename: 'docs/diagram.svg' },
      { filename: '.worker-reports/report.log' }
    ] })).outputs.build, 'false');
    assert.equal((await decision({ event, files: [
      { filename: 'README.md' }, { filename: 'Makefile' }
    ] })).outputs.build, 'true');
    assert.equal((await decision({ event, files: [
      { filename: 'docs/main.c', previous_filename: 'src/main.c' }
    ] })).outputs.build, 'true');
  });
}
test('empty or potentially truncated change lists run', async () => {
  for (const files of [[], Array.from({ length: 300 }, () => ({ filename: 'README.md' }))]) {
    assert.equal((await decision({ files })).outputs.build, 'true');
  }
});
test('manual dispatch does not perform lookups or skip', async () => {
  const result = await decision({ event: 'workflow_dispatch', runs: [success], error: true });
  assert.equal(result.outputs.build, 'true');
  assert.equal(result.calls.length, 0);
});
test('successful same-workflow SHA skips across branches and logs dependency', async () => {
  const result = await decision({ runs: [success] });
  assert.equal(result.outputs.build, 'false');
  assert.ok(result.logs.some(log => log.includes(success.html_url)));
  assert.equal(result.calls[0][1].head_sha, 'sha');
  assert.equal(result.calls[0][1].workflow_id, 'linux.yml');
  assert.equal(result.calls[0][1].branch, undefined);
});
test('own run, wrong SHA/event, failed and cancelled never count', async () => {
  for (const change of [{ id: 10 }, { head_sha: 'other' }, { event: 'pull_request' },
    { conclusion: 'failure' }, { conclusion: 'cancelled' }]) {
    assert.equal((await decision({ runs: [{ ...success, ...change }] })).outputs.build, 'true');
  }
});
test('push lookup errors run; release lookup errors fail explicitly', async () => {
  assert.equal((await decision({ error: true })).outputs.build, 'true');
  assert.equal((await decision({ event: 'release', error: true })).failures.length, 1);
});
test('release with no successful push builds; release never docs-skips', async () => {
  const result = await decision({ event: 'release', files: [{ filename: 'README.md' }] });
  assert.equal(result.outputs.build, 'true');
  assert.ok(result.logs.some(log => log.includes('Release fallback')));
});
test('release requires complete unexpired artifacts and searches older runs', async () => {
  const result = await decision({ event: 'release', runs: [success],
    artifacts: [{ name: 'package', expired: false }] });
  assert.equal(result.outputs.build, 'false');
  assert.equal(result.outputs['run-id'], '11');
  const older = await decision({ event: 'release', runs: [success, { ...success, id: 12 }],
    artifacts: id => id === 11 ? [] : [{ name: 'package', expired: false }] });
  assert.equal(older.outputs['run-id'], '12');
  // Pushes no longer upload packages: a release then builds its own.
  for (const artifacts of [[], [{ name: 'package', expired: true }]]) {
    const fresh = await decision({ event: 'release', runs: [success], artifacts });
    assert.equal(fresh.failures.length, 0);
    assert.equal(fresh.outputs.build, 'true');
    assert.equal(fresh.outputs['run-id'], '');
  }
});
test('attachment changes the name only and retries existing assets', async () => {
  const fs = require('node:fs'), os = require('node:os'), path = require('node:path');
  const cwd = process.cwd(), temp = fs.mkdtempSync(path.join(os.tmpdir(), 'ci-attach-'));
  const data = Buffer.from([0, 255, 13, 10, 42]), calls = [];
  try {
    process.chdir(temp);
    fs.mkdirSync('release-packages');
    fs.writeFileSync('release-packages/opentyrian-linux-arm64.tar.gz', data);
    await require(path.join(cwd, '.github/ci/attach.cjs'))({
      context: { repo: { owner: 'owner', repo: 'repo' }, payload: {
        release: { id: 1, tag_name: 'v1.2.3' }
      } }, packages: ['opentyrian-linux-arm64.tar.gz'], core: { info: () => {} },
      github: { paginate: async () => [{ id: 2, name: 'opentyrian-v1.2.3-linux-arm64.tar.gz' }],
        rest: { repos: { listReleaseAssets: 'assets',
          deleteReleaseAsset: async p => calls.push(['delete', p]),
          uploadReleaseAsset: async p => calls.push(['upload', p])
        } } }
    });
    assert.equal(calls[0][0], 'delete');
    assert.equal(calls[1][1].name, 'opentyrian-v1.2.3-linux-arm64.tar.gz');
    assert.deepEqual(calls[1][1].data, data);
  } finally { process.chdir(cwd); fs.rmSync(temp, { recursive: true, force: true }); }
});
