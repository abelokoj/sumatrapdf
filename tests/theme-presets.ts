import { readFileSync } from "node:fs";
import { resolve } from "node:path";

export async function testit(): Promise<void> {
  const src = readFileSync(resolve(import.meta.dir, "../src/Theme.cpp"), "utf8");
  const presets = src.match(/static Str themesTxt = StrL\(R"\(([\s\S]*?)\)"\);/)?.[1];
  if (!presets) throw new Error("Built-in theme definitions missing");
  const names = [...presets.matchAll(/^\s*Name\s*=\s*(.+)$/gm)];
  if (names.length !== 12) throw new Error(`Expected twelve Pretty presets; found ${names.length}`);
  for (const color of presets.matchAll(/^\s*(\w*Color)\s*=\s*(.+)$/gm)) {
    const value = color[2].trim();
    if (!/^#[0-9a-f]{6}([0-9a-f]{2})?$/i.test(value)) {
      throw new Error(`${color[1]} uses unsupported native color syntax: ${value}`);
    }
  }
  console.log("PASS: all twelve theme presets use native hex colors");
}

if (import.meta.main) await testit();
