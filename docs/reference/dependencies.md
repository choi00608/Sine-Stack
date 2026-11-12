# 의존성과 라이선스 현황

[문서 목록](../README.md) · [개발 안내](../development.md)

## Sine Stack 자체 라이선스

Sine Stack의 자체 코드와 관련 문서는 [MIT License](../../LICENSE)로 제공합니다. 저작권자는 `2026 choi00608`입니다. MIT는 저작권 및 허가 고지를 유지하는 조건으로 사용·수정·재배포·상업적 이용을 허용하며, 소프트웨어는 보증 없이 제공됩니다. 정확한 조건은 LICENSE 원문을 기준으로 합니다.

외부 코드 및 별도 라이선스가 명시된 파일에는 각 원래 라이선스가 적용됩니다. 루트 LICENSE는 외부 코드의 저작권이나 라이선스를 변경하지 않습니다.

## 외부 의존성

| 구성 요소 | 버전 | 관리 방식과 고지 |
|---|---|---|
| JUCE | 8.0.12 | CMake FetchContent로 고정 태그 사용; AGPLv3 또는 JUCE 상용 라이선스 |
| Signalsmith Stretch | 1.3.2 | 양쪽 프로젝트의 Source/ThirdParty에 포함; MIT 고지 포함 |
| Signalsmith Linear | 0.6.6 | 양쪽 프로젝트의 Source/ThirdParty에 포함; MIT 고지 포함 |

Signalsmith Stretch에는 Live 피치 지연 모드를 위한 선택적 `windowAsymmetry` 전달 수정이 있습니다. 원본 커밋과 수정 내용은 각 프로젝트의 외부 코드 README에 기록되어 있습니다.

- [AU 외부 코드 출처](../../sine%20stack%20au/Source/ThirdParty/README.md)
- [VST3 외부 코드 출처](../../sine%20stack%20vst3/Source/ThirdParty/README.md)
- [AU 외부 코드 고지문](../../sine%20stack%20au/Source/ThirdPartyNotices.txt)
- [VST3 외부 코드 고지문](../../sine%20stack%20vst3/Source/ThirdPartyNotices.txt)
- [Signalsmith Stretch 라이선스](../../sine%20stack%20au/Source/ThirdParty/Signalsmith-Stretch-LICENSE.txt)
- [Signalsmith Linear 라이선스](../../sine%20stack%20au/Source/ThirdParty/signalsmith-linear/LICENSE.txt)

위 Signalsmith 고지문은 빌드 시 AU·VST3·Standalone 번들의 Resources에도 복사됩니다.

## JUCE를 포함한 배포물

JUCE 모듈은 AGPLv3와 JUCE 상용 라이선스 중 적용 가능한 조건을 따라 사용합니다. Sine Stack 자체 코드에 MIT를 적용해도 JUCE를 포함한 완성 바이너리 전체가 MIT만으로 배포되는 것은 아닙니다. AGPLv3 방식으로 배포한다면 결합된 프로그램에 대한 AGPLv3의 소스 제공 등 의무를 충족해야 하며, 상용 라이선스 방식이라면 해당 JUCE 계약 조건을 충족해야 합니다.

- [JUCE 8.0.12 라이선스와 포함 의존성 목록](https://github.com/juce-framework/JUCE/blob/8.0.12/LICENSE.md)
- [JUCE 8 라이선스 계약](https://juce.com/legal/juce-8-licence/)
- [GNU AGPLv3 원문](https://www.gnu.org/licenses/agpl-3.0.html)

JUCE 내부에 포함된 SDK·라이브러리도 각 라이선스와 고지를 유지해야 합니다. 재배포 시 Sine Stack의 LICENSE와 사용한 외부 코드의 라이선스·고지를 함께 제공하세요. 이 문서는 기존 배포 파일의 라이선스 준수 검증이나 JUCE 상용 라이선스 보유 여부를 보증하지 않습니다.
