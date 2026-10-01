# Bundled UI font attribution

The application includes unmodified Regular and SemiBold desktop faces for Manrope, Pretendard Std and Public Sans. These are embedded in executable resources and registered with `AddFontMemResourceEx` privately for the current process. They are never installed into Windows, need no network request at runtime, and add no browser or Flutter dependency. System remains available as the default font choice.

All six assets use the SIL Open Font License 1.1. Preserve the complete original notices in redistributed builds:

- [Manrope OFL](licenses/Manrope-OFL.txt): Mikhail Sharanda and project contributors; static desktop assets from the maintained davelab6/manrope distribution.
- [Pretendard Std OFL](licenses/PretendardStd-OFL.txt): the Pretendard project and upstream contributors; standard Latin family, not Pretendard JP or GOV.
- [Public Sans OFL](licenses/PublicSans-OFL.txt): the Public Sans project and upstream contributors.

No glyph outlines, family names or font tables were changed. Filenames were standardized for resource embedding. The header resolver uses the actual GDI family names in each font, including separate static SemiBold face names. Font registration failures fall back to the system font.

| Embedded asset | Pinned source | SHA-256 |
| --- | --- | --- |
| `Manrope-Regular.otf` | [davelab6/manrope@f521bf8a764e](https://github.com/davelab6/manrope/blob/f521bf8a764ebc8d6b0c1ac6d1df75503441eba5/desktop%20font/manrope-regular.otf) | `7d9e533c34f8f2252f23b058ee3d9c4073c02d974655916249b15a8e682b3150` |
| `Manrope-SemiBold.otf` | [davelab6/manrope@f521bf8a764e](https://github.com/davelab6/manrope/blob/f521bf8a764ebc8d6b0c1ac6d1df75503441eba5/desktop%20font/manrope-semibold.otf) | `2cd94a7a2006e07fefa4940d9cf564fec863b178f66744dde1e2bd33f1cb1a36` |
| `PretendardStd-Regular.ttf` | [orioncactus/pretendard@7aeb0698819b](https://github.com/orioncactus/pretendard/blob/7aeb0698819be2b4097dae8ec8fe6a795e5cf3ae/packages/pretendard-std/dist/public/static/alternative/PretendardStd-Regular.ttf) | `1ad55c52b41152d9a24b4da0dded8fdbbe5875a4ac5259e012a67af5f0d73bfc` |
| `PretendardStd-SemiBold.ttf` | [orioncactus/pretendard@7aeb0698819b](https://github.com/orioncactus/pretendard/blob/7aeb0698819be2b4097dae8ec8fe6a795e5cf3ae/packages/pretendard-std/dist/public/static/alternative/PretendardStd-SemiBold.ttf) | `30d0562593bcbc52b0226eeaa085a9c360246974e0b9870b8b3360dedcdb4f41` |
| `PublicSans-Regular.ttf` | [uswds/public-sans@62058987ce57](https://github.com/uswds/public-sans/blob/62058987ce57f64e39a30adc8a512998a3110c70/fonts/ttf/PublicSans-Regular.ttf) | `b577e9bc9887284e90aae5ad0699689ce36b5cd96207efbec68f77f8aed88379` |
| `PublicSans-SemiBold.ttf` | [uswds/public-sans@62058987ce57](https://github.com/uswds/public-sans/blob/62058987ce57f64e39a30adc8a512998a3110c70/fonts/ttf/PublicSans-SemiBold.ttf) | `9f537d607dc78450841dc31c401e3c4ba7a0ba7217e8b34c1be684a983806399` |
