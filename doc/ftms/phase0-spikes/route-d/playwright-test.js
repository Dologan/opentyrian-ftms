const { chromium } = require('playwright-core');
const S = process.argv[2];
(async () => {
  const b = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium', args: ['--no-sandbox','--autoplay-policy=no-user-gesture-required'] });
  const ctx = await b.newContext({ viewport: { width: 1000, height: 700 } });
  const p = await ctx.newPage();
  const logs = [];
  p.on('console', m => { logs.push(m.text()); if (!/GL Driver|ScriptProcessor/.test(m.text())) console.log('[console]', m.text()); });
  p.on('pageerror', e => console.log('[pageerror]', e.stack));
  await p.addInitScript(() => {
    window.__draws = 0; window.__rafs = 0;
    for (const C of [WebGLRenderingContext, WebGL2RenderingContext]) for (const f of ['drawArrays','drawElements','texImage2D','texSubImage2D']) {
      const o = C.prototype[f]; C.prototype[f] = function(...a){ (window.__c=window.__c||{})[f]=((window.__c||{})[f]||0)+1; window.__draws++; return o.apply(this,a); };
    }
    const raf = () => { window.__rafs++; requestAnimationFrame(raf); }; requestAnimationFrame(raf);
  });
  await p.goto('http://localhost:8123/index.html');
  const shot = n => p.screenshot({ path: `${S}/${n}.png` });
  const t0 = Date.now();
  await p.waitForFunction(() => window.Module && Module.ccall, null, {timeout: 30000});
  console.log('time to runtime ready ms', Date.now()-t0);
  await p.waitForTimeout(3000);
  const key = async (k, w=1200) => { await p.keyboard.press(k); await p.waitForTimeout(w); };
  const meas = async (label, ms=5000) => {
    const a = await p.evaluate(() => [window.__draws, window.__rafs]); await p.waitForTimeout(ms);
    const c = await p.evaluate(() => [window.__draws, window.__rafs]);
    console.log(`${label}: draws/s=${((c[0]-a[0])/ms*1000).toFixed(1)} rAF/s=${((c[1]-a[1])/ms*1000).toFixed(1)}`);
  };
  await meas('title menu');
  for (let i = 0; i < 5; i++) { await key('Enter', 1500); }
  await shot('menu-game');
  for (let i = 0; i < 4; i++) await key('ArrowDown', 300);
  await key('Enter', 500);
  await p.waitForTimeout(12000); await shot('lvl-a');
  const cnt = async (label, ms) => {
    const a = await p.evaluate(() => JSON.stringify(window.__c)); await p.waitForTimeout(ms);
    const c = await p.evaluate(() => JSON.stringify(window.__c));
    const A=JSON.parse(a), C=JSON.parse(c); const o={}; for (const k in C) o[k]=((C[k]-(A[k]||0))/ms*1000).toFixed(1);
    console.log(label, 'per-second GL calls', JSON.stringify(o));
  };
  await p.keyboard.down('Space'); await p.keyboard.down('ArrowUp');
  await p.waitForTimeout(3000); await shot('lvl-b');
  await cnt('level gameplay', 10000);
  await shot('lvl-c');
  await p.keyboard.up('Space'); await p.keyboard.up('ArrowUp');
  // FTMS path
  await p.evaluate(() => document.getElementById('btn-fake').click()); await p.waitForTimeout(1500);
  console.log('ftms getter', await p.evaluate(() => [Module.ccall('ftms_web_get_power','number',[],[]), Module.ccall('ftms_web_get_cadence','number',[],[]), Module.ccall('ftms_web_get_seq','number',[],[])]));
  console.log('bar text', await p.textContent('#ftms'));
  // persistence
  console.log('userdir', await p.evaluate(() => { try { return Module.FS.readdir('/home/web_user/.config/opentyrian'); } catch(e){ return String(e);} }));
  console.log('heap MB', await p.evaluate(() => (typeof HEAPU8!=='undefined'?HEAPU8.length:(Module.HEAPU8||{}).length)/1048576));
  await shot('final');
  await b.close();
})();
