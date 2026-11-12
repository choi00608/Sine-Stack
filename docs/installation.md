# 설치 및 업데이트

[문서 목록](README.md) · [사용 설명서](user-guide.md)

## 지원 환경과 형식

현재 배포 대상은 **macOS 11 이상**입니다. Universal 바이너리에 Apple Silicon(`arm64`)과 Intel(`x86_64`) 코드를 함께 포함합니다. Windows·Linux용 VST3나 AAX는 현재 제공하지 않습니다.

- **AU**: Logic Pro·GarageBand처럼 AU 악기를 사용하는 호스트용입니다.
- **VST3**: macOS용 VST3 악기를 지원하는 호스트용입니다.
- **Standalone**: DAW 없이 실행하는 `Sine Stack.app`입니다.

터미널 설치와 `.pkg` 모두 AU·VST3·Standalone을 포함합니다. 같은 Universal 파일을 두 CPU 아키텍처에 사용할 수 있지만 모든 DAW와 실제 Intel 하드웨어에서 검증했다는 뜻은 아닙니다.

## 터미널 설치와 업데이트

Python·Homebrew·Xcode 없이 macOS 기본 도구로 설치합니다. 일반 사용자 계정에서 실행하며 `sudo`를 붙이지 않습니다. 실행 중인 앱·DAW는 직접 종료하고 설치 후 다시 여세요.

[공개 릴리스](https://github.com/choi00608/Sine-Stack/releases)가 게시되면 최신 버전을 받는 명령은 다음과 같습니다. 저장소만 생성되고 릴리스 파일이 아직 없으면 다운로드할 수 없습니다.

```sh
curl -fL --proto '=https' --proto-redir '=https' \
  https://github.com/choi00608/Sine-Stack/releases/latest/download/install.sh -o install.sh && bash install.sh
```

설치 도구의 기본 저장소는 `choi00608/Sine-Stack`입니다. 다른 공개 저장소의 배포물을 사용할 때는 `--repo OWNER/REPOSITORY`로 지정할 수 있습니다. 지정한 저장소는 다음 업데이트에도 사용됩니다.

한 번 설치한 뒤에는 다음 명령만 실행하면 됩니다. 이 명령은 사용자가 실행할 때 최신 릴리스를 확인하며 백그라운드 자동 업데이트를 등록하지 않습니다.

```sh
bash "$HOME/Library/Application Support/SineStack/Installer/install.sh"
```

### 로컬 파일로 설치

같은 릴리스의 `install.sh`, `SineStack-버전-macOS-Universal.tar.gz`, `SHA256SUMS.txt`를 한 폴더에 다운로드하거나 복사합니다. 그 폴더에서 실행합니다.

```sh
bash install.sh --archive "SineStack-0.1.3-macOS-Universal.tar.gz"
```

새 버전도 해당 버전의 세 파일을 받아 같은 방식으로 설치합니다. 네트워크 연결은 필요하지 않습니다.

### 옵션과 검증

```sh
# 파일 검증과 설치 경로 확인만 수행
bash install.sh --archive "SineStack-0.1.3-macOS-Universal.tar.gz" --check
# 공개된 특정 버전 선택 (태그 v0.1.2)
bash install.sh --version 0.1.2
# 의도한 다운그레이드만 명시적으로 허용
bash install.sh --version 0.1.2 --allow-downgrade
```

설치 도구는 SHA-256, 아카이브 경로, 세 번들의 버전·빌드 정보·Universal 구성·서명을 검사한 뒤 교체합니다. 기본적으로 다운그레이드를 거부합니다. 교체 중 오류가 나면 기존 설치본을 복원하며, 복원하지 못한 파일은 복구 경로를 출력하고 남겨 둡니다. 체크섬은 파일 일치 여부를 확인하며 배포자 인증이나 Apple 공증을 대신하지 않습니다.

| 항목 | 터미널 설치 위치 |
|---|---|
| AU | `~/Library/Audio/Plug-Ins/Components/Sine Stack.component` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/Sine Stack.vst3` |
| 앱 | `~/Applications/Sine Stack.app` |
| 업데이트 도구 | `~/Library/Application Support/SineStack/Installer/` |

## 패키지 설치

1. [Releases](https://github.com/choi00608/Sine-Stack/releases)에서 `SineStack-버전-macOS-Universal-unsigned.pkg`를 받습니다. GitHub의 `Source code (zip)`은 개발 소스입니다.
2. 앱과 DAW를 종료한 뒤 `.pkg`를 엽니다.
3. **AU / VST3 / Sine Stack.app** 중 필요한 항목을 선택합니다. 기본값은 세 가지 모두입니다.
4. 관리자 인증을 거쳐 설치하고 앱·DAW를 다시 엽니다.

| 형식 | 패키지 설치 위치 |
|---|---|
| AU | `/Library/Audio/Plug-Ins/Components/Sine Stack.component` |
| VST3 | `/Library/Audio/Plug-Ins/VST3/Sine Stack.vst3` |
| 앱 | `/Applications/Sine Stack.app` |

내장 프리셋 10개는 번들에 포함되어 별도 설치가 필요하지 않습니다. 설치 도구는 사용자 프리셋과 설정을 유지합니다.

같은 릴리스의 배포 파일 4개와 `SHA256SUMS.txt`를 모두 받았다면 해당 폴더에서 검사합니다. 일부 파일만 받았다면 없는 파일은 검사 실패로 표시됩니다.

```sh
shasum -a 256 -c SHA256SUMS.txt
```

현재 패키지는 Developer ID 서명과 Apple 공증이 없고, 번들은 ad-hoc 서명입니다. macOS가 실행을 차단한다면 출처를 확인한 뒤 [Apple의 앱 실행 안내](https://support.apple.com/ko-kr/102445)를 참고하세요.

## Standalone 앱 사용

앱에서 **Options → Audio/MIDI Settings…**를 열어 오디오 출력 장치와 MIDI 입력을 선택합니다. 연결한 MIDI 건반 또는 화면 아래 건반으로 연주합니다.

**Bluetooth MIDI** 버튼은 macOS의 연결 창을 엽니다. 블루투스 사용 권한을 요청하면 연결을 위해 허용해야 합니다. 이 앱에는 해당 권한 설명이 포함되어 있습니다.

**SAMPLE RATE**에서 44.1 / 48 kHz 및 장치가 지원하는 레이트를 선택할 수 있습니다. AU·VST3에서는 호스트가 샘플레이트를 정합니다.

## DAW에서 불러오기

### Logic Pro

소프트웨어 악기 트랙의 **Instrument 슬롯 → AU Instruments → SineStack → Sine Stack → Stereo**에서 선택합니다. 오디오 FX 슬롯에서는 찾을 수 없습니다.

목록에 없다면 **Logic Pro → Settings → Plug-in Manager**에서 Sine Stack을 찾아 **Reset & Rescan Selection**을 실행한 뒤 다시 확인합니다. 메뉴 언어에 따라 표시 이름은 다를 수 있습니다.

### VST3 호스트

DAW의 악기 또는 플러그인 브라우저에서 **SineStack / Sine Stack**을 검색합니다. 목록에 없다면 해당 DAW의 VST3 재검색 기능을 사용합니다. 현재 제공 파일은 macOS용이므로 Windows 플러그인 폴더에 복사해도 실행되지 않습니다.

## 업데이트와 중복 설치

1. 앱·DAW를 완전히 종료합니다.
2. 터미널 업데이트 명령 또는 새 버전의 `.pkg`로 설치합니다.
3. 다시 실행한 뒤 **건반 아래 오른쪽의 버전·빌드 시각**을 확인합니다.

터미널 설치와 개발용 설치 명령은 아래 **사용자 전용 경로**를 사용합니다. `.pkg`의 시스템 전체 경로와 다릅니다.

| 형식 | 사용자 전용 설치 위치 |
|---|---|
| AU | `~/Library/Audio/Plug-Ins/Components/Sine Stack.component` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/Sine Stack.vst3` |
| 앱 | `~/Applications/Sine Stack.app` |

터미널 설치는 시스템 전체 설치본을 갱신하지 않으며 `.pkg`는 사용자 전용 설치본을 갱신하지 않습니다. 두 위치에 구버전과 신버전이 함께 있으면 호스트가 구버전을 불러올 수 있습니다. 사용하지 않을 복사본을 플러그인 검색 폴더 밖에 백업하고 DAW를 재검색하세요. 프로젝트의 `build-before-move` 폴더는 구형 빌드 백업이므로 실행·설치 원본으로 사용하지 않습니다.

소스에서 수정하는 경우 루트의 `python3 scripts/update_local.py`로 AU·VST3·설치된 앱을 함께 갱신합니다. 자세한 명령은 [개발 안내](development.md)에 있습니다.

## 문제 해결 및 제거

| 증상 | 확인할 내용 |
|---|---|
| 다운로드 404 / 실패 | 공개된 정식 릴리스와 필요한 Assets가 있는지, 네트워크 상태 |
| 체크섬 / 서명 오류 | 같은 릴리스의 파일을 다시 다운로드; 기존 설치본은 유지됨 |
| 플러그인이 보이지 않음 | AU/VST3 형식, 악기 슬롯, 설치 경로, DAW 재검색 |
| 업데이트했는데 구버전 표시 | 실행 중이던 앱·DAW 종료, 사용자/시스템 경로의 중복 복사본 |
| 소리가 나지 않음 | MIDI 입력·오디오 출력, 소스 블록 유무·바이패스, Output |
| 프리셋 목록이 비어 있음 | Factory / User 선택, 카테고리 필터, User 폴더, Refresh |
| 연주 반응이 느림 | 오디오 버퍼, 피치 FX 개수, PITCH LATENCY의 Live / Detail |

제거하려면 앱·DAW를 종료한 뒤 설치된 `.component`, `.vst3`, `.app` 중 제거할 항목을 휴지통으로 옮깁니다. 따로 저장한 프리셋과 사용자 설정은 그대로 남습니다.
