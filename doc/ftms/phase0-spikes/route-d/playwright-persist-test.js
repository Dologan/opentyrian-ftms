const { chromium } = require('playwright-core');
(async()=>{const b=await chromium.launch({executablePath:'/opt/pw-browsers/chromium',args:['--no-sandbox']});const ctx=await b.newContext();const p=await ctx.newPage();
p.on('pageerror',e=>console.log('[err]',e.message));
await p.goto('http://localhost:8123/index.html');await p.waitForTimeout(4000);
await p.evaluate(()=>Module.FS.writeFile('/home/web_user/.config/opentyrian/tyrian.cfg','hello'));
await p.waitForTimeout(6500);   // periodic 5s IDBFS flush
console.log('before reload', await p.evaluate(()=>Module.FS.readdir('/home/web_user/.config/opentyrian').join(',')));
await p.reload();await p.waitForTimeout(4000);
console.log('after reload', await p.evaluate(()=>Module.FS.readdir('/home/web_user/.config/opentyrian').join(',')));
console.log('audio ctx', await p.evaluate(()=>{try{return (window.SDL&&SDL.audioContext&&SDL.audioContext.state)||(typeof SDL!=='undefined'?'sdl-no-ctx':'no SDL global')}catch(e){return String(e)}}));
await b.close();})();
