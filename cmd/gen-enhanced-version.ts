import { readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

export function enhancedVersionHeader(value: string): string {
  const version = value.trim();
  const match = /^v(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*))?$/.exec(version);
  if (!match || match.slice(1, 4).some((part) => Number(part) > 65535)) {
    throw new Error("enhanced-version.txt must contain a version such as v0.1.1, with components from 0 to 65535");
  }
  const display = version.substring(1);
  return `// Generated from enhanced-version.txt by cmd/build.ts.\n#define ENHANCED_VERSION_STRA "${display}"\n#define ENHANCED_VERSION_RESOURCE ${match[1]}, ${match[2]}, ${match[3]}, 0\n`;
}

export function generateEnhancedVersion(root = process.cwd()): void {
  const text = enhancedVersionHeader(readFileSync(join(root, "enhanced-version.txt"), "utf-8"));
  const path = join(root, "src", "EnhancedVersion.h");
  let previous = "";
  try {
    previous = readFileSync(path, "utf-8");
  } catch {}
  if (previous.replace(/\r\n/g, "\n") !== text) writeFileSync(path, text, "utf-8");
}
