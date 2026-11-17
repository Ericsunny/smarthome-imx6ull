import fs from 'node:fs';
import path from 'node:path';
import {JSDOM} from 'jsdom';
const dom = new JSDOM('<!doctype html><html><body></body></html>');
globalThis.window = dom.window;
globalThis.document = dom.window.document;
const {default: mermaid} = await import('mermaid');
mermaid.initialize({startOnLoad:false, securityLevel:'strict'});
const root = process.cwd();
const architecture = fs.readFileSync(path.join(root, 'docs/overview/architecture.md'), 'utf8');
const diagrams = [...architecture.matchAll(/```mermaid\r?\n([\s\S]*?)```/g)];
if (diagrams.length !== 3) throw new Error('Expected three current architecture diagrams');
const types = [];
for (const diagram of diagrams) types.push((await mermaid.parse(diagram[1])).diagramType);
const files = ['README.md', '02-应用层QT/README.md',
    ...fs.readdirSync('docs', {recursive:true}).filter(x => x.endsWith('.md')).map(x => path.join('docs', x))];
let links = 0;
const broken = [];
for (const file of files) {
    const source = fs.readFileSync(file, 'utf8');
    for (const match of source.matchAll(/\[[^\]]*\]\(([^)]+)\)/g)) {
        let target = match[1];
        if (/^(https?:|mailto:|#)/.test(target)) continue;
        target = decodeURIComponent(target.split('#')[0]);
        ++links;
        if (!fs.existsSync(path.resolve(path.dirname(file), target))) broken.push({file, target});
    }
}
if (broken.length) throw new Error(JSON.stringify(broken));
console.log(JSON.stringify({pass:true, diagrams:diagrams.length, types,
    parser:'mermaid 11.12.0', local_links:links, broken}));
