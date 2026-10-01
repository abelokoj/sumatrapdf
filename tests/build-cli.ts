import { join } from "node:path";
import { ROOT } from "./util.ts";

function run(args: string[]): { code: number; stdout: string; stderr: string } {
  const proc = Bun.spawnSync(["bun", join(ROOT, "cmd", "build.ts"), ...args], { cwd: ROOT });
  return {
    code: proc.exitCode,
    stdout: proc.stdout.toString(),
    stderr: proc.stderr.toString(),
  };
}

function check(condition: boolean, message: string): void {
  if (!condition) throw new Error(message);
}

export async function testit(): Promise<void> {
  const noArgs = run([]);
  check(noArgs.code === 0, `no-argument exit code: ${noArgs.code}`);
  check(noArgs.stdout.includes("Usage: bun cmd/build.ts"), "no-argument invocation did not print usage");

  const unknown = run(["-not-a-build-option"]);
  check(unknown.code !== 0, "unknown option succeeded");
  check(unknown.stderr.includes("unknown option: -not-a-build-option"), "unknown option did not print an error");
  check(unknown.stderr.includes("Usage: bun cmd/build.ts"), "unknown option did not print usage");

  const conflict = run(["-mingw", "-dbg", "-rel"]);
  check(conflict.code !== 0, "conflicting configurations succeeded");
  check(conflict.stderr.includes("cannot be used together"), "configuration conflict did not explain the error");
  for (const args of [
    ["-dbg", "-static"],
    ["-rel", "-asan", "-static"],
    ["-mingw", "-rel", "-static"],
  ]) {
    const invalidStatic = run(args);
    check(invalidStatic.code !== 0, `invalid static build succeeded: ${args.join(" ")}`);
    check(invalidStatic.stderr.includes("-static requires"), "static validation did not explain the error");
  }
  const duplicateStatic = run(["-rel", "-static", "-static"]);
  check(duplicateStatic.code !== 0, "duplicate static option succeeded");
  check(
    duplicateStatic.stderr.includes("-static can only be specified once"),
    "duplicate static option did not explain the error",
  );
  console.log("PASS: unified build CLI validation");
}

if (import.meta.main) {
  try {
    await testit();
  } catch (error) {
    console.error(`❌ ${error instanceof Error ? error.message : error}`);
    process.exitCode = 1;
  }
}
