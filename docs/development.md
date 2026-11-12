# 개발 및 빌드

[문서 목록](README.md) · [버전·배포](releases.md) · [기여 안내](../CONTRIBUTING.md)

## 준비

- macOS와 Xcode Command Line Tools
- CMake 3.22 이상
- Python 3 (`fcntl`을 사용하는 macOS용 버전·설치 도구)
- 첫 JUCE 다운로드 시 인터넷 연결

JUCE는 8.0.12로 고정됩니다. Signalsmith DSP 헤더는 저장소에 포함되어 있습니다. 배포된 플러그인·실행 앱을 사용하는 사람에게 CMake나 Python은 필요하지 않습니다.

AU·VST3 프로젝트는 소스 복사본이 각각 있지만 공통 버전 관리는 저장소 루트에 의존합니다. 한 폴더만 떼어 복사하지 말고 저장소 전체 구조를 유지하세요. 아래 명령은 별도 안내가 없으면 **저장소 루트**에서 실행합니다.

## 첫 구성과 Universal 빌드

```sh
(cd "sine stack au" && cmake --preset universal)
(cd "sine stack vst3" && cmake --preset universal)
python3 scripts/update_local.py --build-only
```

Universal 프리셋은 Release, `arm64;x86_64`, 최소 macOS 11, 테스트 활성화를 설정합니다. `--build-only`는 AU·VST3·Standalone을 빌드하고 버전·서명·아키텍처를 검증하지만 시스템의 설치본을 교체하지 않습니다.

기존 JUCE 소스를 사용할 때는 각 구성 명령에 `-DFETCHCONTENT_SOURCE_DIR_JUCE=/absolute/path/to/JUCE`를 추가할 수 있습니다. CMake가 PATH에 없다면 통합 명령의 `--cmake /absolute/path/to/cmake` 옵션을 사용하세요.

| 결과물 | 경로 |
|---|---|
| AU | `sine stack au/build/SineStack_artefacts/Release/AU/Sine Stack.component` |
| VST3 | `sine stack vst3/build/SineStack_artefacts/Release/VST3/Sine Stack.vst3` |
| 실행 앱 | `sine stack au/build/SineStack_artefacts/Release/Standalone/Sine Stack.app` |

VST3 프로젝트도 Standalone을 빌드하지만 사용자 앱 설치에는 AU 프로젝트의 Standalone을 사용합니다. `build-before-move`는 이전 경로의 빌드 백업이며 재사용하지 않습니다.

## 수정 후 로컬 설치

공통 기능 수정은 두 프로젝트의 Source에 함께 반영합니다. 검증을 마치면 다음 명령으로 세 설치본을 함께 갱신합니다.

```sh
python3 scripts/update_local.py
```

이 명령은 공통 버전을 준비하고 양쪽 프로젝트를 구성·빌드한 뒤, 모든 번들의 검증과 임시 복사를 완료하고 기존 설치본을 교체합니다. 교체 실패 시 이전 설치본 복원을 시도합니다. 소스가 빌드 중 변경되거나 번들의 버전·빌드 시각·소스 식별 정보가 다르면 설치를 중단합니다.

설치 위치는 [설치 안내](installation.md)의 사용자 전용 경로입니다. 프리셋과 설정 파일은 교체 대상이 아닙니다. 실행 중인 앱이나 DAW를 강제로 종료하지 않으므로 설치 후 직접 완전히 종료했다가 다시 여세요.

개별 형식만 개발할 때는 각 프로젝트의 `cmake --build build`를 사용할 수 있습니다. 다만 최종 설치·배포는 통합 명령으로 공통 버전을 맞춥니다. 공통 버전 규칙은 [버전 관리](releases.md#공통-버전-규칙)에 있습니다.

## 검사

오디오·UI 검사와 VST3 호스트 검사를 실행하려면 테스트 실행 파일까지 빌드합니다. 통합 `--build-only`만으로는 모든 테스트 타깃을 빌드하지 않습니다.

```sh
cmake --build "sine stack au/build" --parallel 3
cmake --build "sine stack vst3/build" --parallel 3
ctest --test-dir "sine stack au/build" --output-on-failure
ctest --test-dir "sine stack vst3/build" --output-on-failure

python3 -m unittest discover -s scripts/tests -v
python3 scripts/tests/check_cmake_version.py cmake
```

배포 파일 생성과 설치 도구의 임시 환경 검사는 [배포 안내](releases.md#배포-파일-만들기)를 참고하세요.

버전 검사는 소스·배포 도구 변경 감지, 병렬 구성 시 단일 증가, 무변경 빌드, 재시도, 오래된 번들 거부와 실제 CMake 재구성을 확인합니다. VST3 호스트 검사는 실제 플러그인 로드·호스트 버전·MIDI·템포·상태 복원을 확인합니다.

설치된 AU는 다음 명령으로 검사합니다.

```sh
auval -v aumu Snst Snst -strict -qp
```

대상 기능만 확인하는 테스트 옵션도 있습니다.

```sh
(cd "sine stack au/build" && ./SineStackChecks_artefacts/Release/SineStackChecks --check-categories)
(cd "sine stack au/build" && ./SineStackChecks_artefacts/Release/SineStackChecks --check-themes)
(cd "sine stack au/build" && ./SineStackChecks_artefacts/Release/SineStackChecks --check-factory)
```

GUI 창이 필요한 검사는 디스플레이가 없는 환경에서 일부 생략될 수 있습니다. 테마 검사는 임시 설정 파일을 사용합니다. Apple Silicon에서 Rosetta로 x86_64 실행을 확인하는 것과 실제 Intel 하드웨어 검증은 구분합니다.

## 소스 구조

| 위치 | 역할 |
|---|---|
| 각 프로젝트의 `Source/Plugin.cpp`, `Plugin.h` | 오디오 처리·파라미터·블록 모델·프리셋·호스트 상태 |
| `Source/Editor.h`, `EqGraph.h` | 체인·모듈레이션·설정 패널·그래프 UI |
| `Source/SourceSettings.h`, `FxDetails.h`, `EqSettings.h` | 블록별 파라미터 정의 |
| `Source/FactoryPresets.h`, `PresetCategories.h` | 내장 사운드와 카테고리 |
| `Source/DesignTheme.h` | 디자인 프리셋과 별도 테마 설정 |
| `Source/Standalone.cpp`, `AudioSettings.h` | 독립 실행 앱·오디오 장치 연결 |
| `Source/ThirdParty/` | 포함된 피치 변환 DSP 코드와 라이선스 |
| `Tests/` | 오디오·상태·UI 검사; VST3 쪽에는 별도 호스트 검사 |
| 루트 `cmake/SharedVersion.cmake`, `scripts/versioning.py` | 공통 버전·빌드 시각 준비 |
| 각 프로젝트의 `cmake/BuildInfo.cmake` | 실행 바이너리의 버전·빌드 시각 생성 |
| `scripts/update_local.py` | 전체 빌드·검증·사용자 설치 |
| `assets/SineStackIcon.png` | 양쪽 Standalone 빌드에서 사용하는 앱 아이콘 원본; JUCE 번들 설정 후 `cmake/AppIcon.cmake`가 1x/2x 전체 크기의 `.icns` 생성 |
| `packaging/` | 터미널 설치·업데이트, 통합 `.pkg`·아카이브 생성과 검증 |

`getAudioChain()`은 실제 오디오 처리 순서를, `getModulators()`는 독립 제어 영역을 제공합니다. 저장 상태의 `chain` 필드는 호환성을 위해 전체 블록 등록 목록을 유지합니다. 저장 형식을 변경할 때에는 기존 프리셋과 파라미터 ID 호환성을 함께 확인하세요.

빌드 캐시, 산출물, 임시 버전 잠금 파일은 `.gitignore`로 제외합니다. 문서용 화면 이미지는 `docs/images/`에 저장하고, 테스트 생성 이미지는 빌드 폴더에 둡니다.
