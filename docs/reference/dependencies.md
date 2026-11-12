# 의존성과 라이선스 현황

[문서 목록](../README.md) · [개발 안내](../development.md)

| 구성 요소 | 버전 | 관리 방식과 고지 |
|---|---|---|
| JUCE | 8.0.12 | CMake FetchContent로 고정 태그 사용; JUCE 소스에 포함된 라이선스 확인 |
| Signalsmith Stretch | 1.3.2 | 양쪽 프로젝트의 Source/ThirdParty에 포함; MIT 고지 포함 |
| Signalsmith Linear | 0.6.6 | 양쪽 프로젝트의 Source/ThirdParty에 포함; MIT 고지 포함 |

Signalsmith Stretch에는 Live 피치 지연 모드를 위한 선택적 `windowAsymmetry` 전달 수정이 있습니다. 원본 커밋과 수정 내용은 각 프로젝트의 외부 코드 README에 기록되어 있습니다.

- [AU 외부 코드 출처](../../sine%20stack%20au/Source/ThirdParty/README.md)
- [VST3 외부 코드 출처](../../sine%20stack%20vst3/Source/ThirdParty/README.md)
- [AU 외부 코드 고지문](../../sine%20stack%20au/Source/ThirdPartyNotices.txt)
- [VST3 외부 코드 고지문](../../sine%20stack%20vst3/Source/ThirdPartyNotices.txt)
- [Signalsmith Stretch 라이선스](../../sine%20stack%20au/Source/ThirdParty/Signalsmith-Stretch-LICENSE.txt)
- [Signalsmith Linear 라이선스](../../sine%20stack%20au/Source/ThirdParty/signalsmith-linear/LICENSE.txt)

고지문은 빌드 시 AU·VST3·Standalone 번들의 Resources에도 복사됩니다. 프로젝트 자체의 라이선스는 아직 지정되지 않았습니다. 이 문서는 포함된 파일과 설정을 정리한 것이며 새 배포 라이선스를 부여하지 않습니다.
