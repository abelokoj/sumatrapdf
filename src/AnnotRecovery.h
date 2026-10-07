/* Copyright 2026 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct WindowTab;

void AnnotRecoveryChanged(WindowTab*);
void AnnotRecoveryForget(WindowTab*);
void AnnotRecoveryClose(WindowTab*);
void AnnotRecoveryShutdown();
TempStr AnnotRecoveryFind(Str source, bool& changedSource);
bool AnnotRecoveryDiscard(Str recoveryCopy);
TempStr AnnotRecoveryStatus(WindowTab*);

#if IS_DEBUG
bool AnnotRecovery_UnitTests();
#endif
