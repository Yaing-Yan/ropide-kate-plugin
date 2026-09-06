#!/usr/bin/env node
/*
 * SPDX-FileCopyrightText: 2026 Yaing-Yan
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * 编译器移植交叉验证：用 node 跑原插件 ropide-vscode-plugin 的 media/compiler.js，
 * 与 C++ 移植版（ropide-test-compiler）对拍同一组测试用例，逐字段比对。
 *
 * 用法：
 *   ROPIDE_VSCODE_PLUGIN_DIR=/path/to/ropide-vscode-plugin \
 *   CPP_BIN=build/ropide-test-compiler node tests/crosscheck.mjs
 */
import { readFileSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import vm from 'node:vm';
import path from 'node:path';

const vscodeDir =
  process.env.ROPIDE_VSCODE_PLUGIN_DIR || path.resolve('../ropide-vscode-plugin');
const cppBin = process.env.CPP_BIN || path.resolve('build/ropide-test-compiler');

const cases = JSON.parse(readFileSync(new URL('./cases.json', import.meta.url), 'utf8'));

// 1) node 侧：加载原版编译器
const src = readFileSync(path.join(vscodeDir, 'media/compiler.js'), 'utf8');
const sandbox = { window: {} };
vm.createContext(sandbox);
vm.runInContext(src, sandbox);
const jsParse = sandbox.window.RopCompiler.parseRopInput;

const jsOut = cases.map((c) => {
  const r = jsParse(c.input, c.gadgets, {
    leftStartAddress: c.leftStartAddress,
    rightStartAddress: c.rightStartAddress,
  });
  return {
    name: c.name,
    hexChars: r.hexChars,
    byteStartPositions: r.byteStartPositions,
    charPosInInputMap: r.charPosInInputMap,
    errorCount: r.errorCount,
    totalBytes: r.totalBytes,
    constants: r.constants,
    anchorSides: r.anchorSides,
    highlightLines: r.highlightLines,
  };
});

// 2) C++ 侧
const cppStdout = execFileSync(cppBin, [new URL('./cases.json', import.meta.url).pathname], {
  encoding: 'utf8',
});
const cppOut = cppStdout
  .split('\n')
  .filter((l) => l.trim())
  .map((l) => JSON.parse(l));

if (jsOut.length !== cppOut.length) {
  console.error(`case count mismatch: js=${jsOut.length} cpp=${cppOut.length}`);
  process.exit(1);
}

// 3) 比对
const deepEqual = (a, b) => JSON.stringify(a) === JSON.stringify(b);
// 键序无关（C++ 侧 QJsonObject 按字典序输出，JS 对象按插入序，但语义等价）
const normConstants = (o) =>
  Object.fromEntries(
    Object.entries(o)
      .map(([k, v]) => [k, typeof v === 'number' ? v : NaN])
      .sort(([a], [b]) => (a < b ? -1 : 1)),
  );
const normHighlight = (lines) =>
  JSON.stringify(lines.map((spans) => spans.map((s) => `${s.type}\u0000${s.content}`)));
let failed = 0;
for (let i = 0; i < jsOut.length; i++) {
  const a = jsOut[i];
  const b = cppOut[i];
  const diffs = [];
  if (a.name !== b.name) diffs.push(`name: ${a.name} vs ${b.name}`);
  if (a.hexChars !== b.hexChars) diffs.push(`hexChars: ${JSON.stringify(a.hexChars)} vs ${JSON.stringify(b.hexChars)}`);
  if (deepEqual(a.byteStartPositions, b.byteStartPositions) === false)
    diffs.push(`byteStartPositions: ${JSON.stringify(a.byteStartPositions)} vs ${JSON.stringify(b.byteStartPositions)}`);
  if (deepEqual(a.charPosInInputMap, b.charPosInInputMap) === false)
    diffs.push(`charPosInInputMap: ${JSON.stringify(a.charPosInInputMap)} vs ${JSON.stringify(b.charPosInInputMap)}`);
  if (a.errorCount !== b.errorCount) diffs.push(`errorCount: ${a.errorCount} vs ${b.errorCount}`);
  if (a.totalBytes !== b.totalBytes) diffs.push(`totalBytes: ${a.totalBytes} vs ${b.totalBytes}`);
  if (deepEqual(normConstants(a.constants), normConstants(b.constants)) === false)
    diffs.push(`constants: ${JSON.stringify(a.constants)} vs ${JSON.stringify(b.constants)}`);
  if (normHighlight(a.highlightLines) !== normHighlight(b.highlightLines))
    diffs.push(`highlightLines: ${JSON.stringify(a.highlightLines)} vs ${JSON.stringify(b.highlightLines)}`);
  if (deepEqual(a.anchorSides, b.anchorSides) === false)
    diffs.push(`anchorSides: ${JSON.stringify(a.anchorSides)} vs ${JSON.stringify(b.anchorSides)}`);
  if (diffs.length) {
    failed++;
    console.error(`FAIL ${a.name}`);
    for (const d of diffs) console.error('  ' + d);
  } else {
    console.log(`PASS ${a.name}`);
  }
}
console.log(`\n${jsOut.length - failed}/${jsOut.length} cases passed`);
process.exit(failed ? 1 : 0);
