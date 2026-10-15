// Shared by the three OS workflows; github-script supplies Octokit and context.
// Fail open for read-only lookup errors: an uncertain result must run the build.
module.exports = async ({ github, context, core, workflow, packages }) => {
  const repo = context.repo;
  const output = (build, run = '') => {
    core.setOutput('build', String(build));
    core.setOutput('run-id', String(run));
  };
  output(true);
  if (!['push', 'pull_request', 'release'].includes(context.eventName)) {
    core.info('Manual run: build and test normally.');
    return;
  }
  try {
    let sha = context.sha;
    if (context.eventName === 'release') {
      // Resolve annotated tags as well as lightweight tags to the commit SHA.
      sha = (await github.rest.repos.getCommit({ ...repo,
        ref: context.payload.release.tag_name })).data.sha;
    }
    if (context.eventName !== 'release') {
      const pr = context.payload.pull_request;
      const base = pr ? pr.base.sha : context.payload.before;
      const head = pr ? pr.head.sha : sha;
      if (base && !/^0+$/.test(base)) {
        const comparison = (await github.rest.repos.compareCommitsWithBasehead({
          ...repo, basehead: `${base}...${head}`
        })).data;
        const files = comparison.files || [];
        const docs = name => /\.md$/i.test(name) || name.startsWith('docs/') ||
          name.startsWith('.worker-reports/');
        // Compare returns at most 300 files. A truncated/empty list is not proof.
        // Renames must have documentation-only old AND new names.
        if (files.length > 0 && files.length < 300 && files.every(file =>
          docs(file.filename) && (!file.previous_filename || docs(file.previous_filename)))) {
          core.info('Documentation-only change: skip build/regression; checks finish green.');
          output(false);
          return;
        }
      }
      if (context.eventName === 'pull_request') return;
    }
    // No branch filter: a fast-forward to master can reuse a feature branch run.
    const runs = await github.paginate(github.rest.actions.listWorkflowRuns, {
      ...repo, workflow_id: workflow, event: 'push', status: 'success',
      head_sha: sha, per_page: 100
    });
    const successful = runs.filter(run => run.id !== context.runId &&
      run.head_sha === sha && run.event === 'push' && run.conclusion === 'success');
    if (context.eventName === 'push') {
      if (successful.length) {
        core.info(`Already tested ${sha}: relying on ${successful[0].html_url}`);
        output(false);
      } else core.info(`No successful push run for ${sha}: build and test normally.`);
      return;
    }
    for (const run of successful) {
      const artifacts = await github.paginate(github.rest.actions.listWorkflowRunArtifacts,
        { ...repo, run_id: run.id, per_page: 100 });
      if (packages.every(name => artifacts.some(a => a.name === name && !a.expired))) {
        core.info(`Release reuses packages from ${run.html_url}`);
        output(false, run.id);
        return;
      }
    }
    // Push and pull_request runs no longer upload packages (they used up the
    // Actions storage quota), so a release builds its own packages here.
    core.info(successful.length
      ? `Release: successful push runs for ${sha} have no unexpired packages; build packages normally.`
      : `Release fallback: no successful push run for ${sha}; build packages normally.`);
  } catch (error) {
    if (context.eventName === 'release') {
      // Release fallback is allowed only after proving no successful run exists.
      core.setFailed(`Release lookup failed; refusing an unverified rebuild: ${error.message}`);
    } else core.warning(`CI lookup failed; running build/regression: ${error.message}`);
  }
};
