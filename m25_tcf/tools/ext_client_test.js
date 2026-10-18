/*
 * ext_client_test.js - drive OUR tcf-agent through the VS Code extension's
 * own compiled TCF client (the same code the debug adapter uses).
 */
'use strict';

const base = 'C:/Users/admin/codex/aurix_tcf/vscode-aurix-tcf-debugger/out';
const { TCFClient } = require(base + '/tcfclient');
const { DebugCommandStamper } = require(base + '/tcf');

const logger = {
    sent: (m) => { },
    received: (m) => { },
    log: (m) => console.log('[log]', m),
    error: (m) => console.log('[err]', m),
    stdout: (m) => console.log('[out]', m),
};

(async () => {
    const client = new TCFClient(logger, new DebugCommandStamper());
    console.log('connecting...');
    await client.connect('127.0.0.1', 1534);
    console.log('connected');

    const threads = await client.getThreads();
    console.log(`threads: ${threads.length}`);
    for (const t of threads) {
        console.log(`  - ${t.info.ID} "${t.info.Name}" state=${JSON.stringify(t.state)}`);
    }

    const first = threads[0];
    if (first) {
        // The adapter suspends all running contexts at startup.
        console.log(`suspending ${first.info.ID}...`);
        try {
            await client.sendCommand(new (require(base + '/tcf/runcontrol').SuspendRunControlCommand)(first.info.ID));
        } catch (e) {
            console.log('   suspend error:', JSON.stringify(e));
        }
        await new Promise((r) => setTimeout(r, 800));

        const threads2 = await client.getThreads();
        for (const t of threads2) {
            console.log(`  after suspend: ${t.info.ID} state=${JSON.stringify(t.state)}`);
        }

        console.log(`stack trace (simple) for ${first.info.ID}:`);
        try {
            const stack = await client.getStackTrace(first.info.ID, undefined, -1);
            console.log(`   frames: ${stack.length}`);
            for (const f of stack) {
                console.log(`   frame ${f.Level}: ip=0x${(f.IP >>> 0).toString(16)} func=${f.Function || ''}`);
            }
        } catch (e) {
            console.log('   stack error:', JSON.stringify(e));
        }
        console.log(`stack trace (depth 8) for ${first.info.ID}:`);
        try {
            const stack = await client.getStackTrace(first.info.ID, undefined, 8);
            console.log(`   frames: ${stack.length}`);
            for (const f of stack) {
                console.log(`   frame ${f.Level}: ip=0x${(f.IP >>> 0).toString(16)} func=${f.Function || ''}`);
            }
        } catch (e) {
            console.log('   stack error:', JSON.stringify(e));
        }
    }

    console.log('=== ext client test done ===');
    process.exit(0);
})().catch((e) => {
    console.error('FAILED:', e);
    process.exit(1);
});
