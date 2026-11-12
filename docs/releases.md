# 버전 관리와 GitHub 배포

[문서 목록](README.md) · [개발·빌드](development.md) · [변경 이력](../CHANGELOG.md)

## 저장소와 배포 파일 구분

소스·테스트·문서·프리셋은 [choi00608/Sine-Stack](https://github.com/choi00608/Sine-Stack)에 보관하고, 완성 파일은 **GitHub Releases의 Assets**로 배포합니다. `build/`, `build-before-move/`, `dist/`, 임시 파일은 Git에 올리지 않습니다. 두 소스 폴더, 루트 `cmake/`, `scripts/`, `packaging/`, `version.json`과 외부 코드 고지문을 함께 유지합니다.

아직 로컬 Git을 초기화하지 않았다면 루트에서 다음 순서로 연결합니다. 이미 연결한 저장소에서는 이 초기화 절차를 반복하지 않습니다.

```sh
git init -b main
git add .
git diff --cached --stat
git commit -m "Prepare Sine Stack source and distribution tools"
git remote add origin https://github.com/choi00608/Sine-Stack.git
git push -u origin main
```

처음 공개하기 전 파일 목록에서 개인 프리셋·절대 경로·빌드 캐시가 섞이지 않았는지 확인합니다. 프로젝트 자체 라이선스는 아직 지정되지 않았으므로 저장소에 적용할 라이선스는 관리자가 정합니다. 외부 코드 고지는 [의존성 안내](reference/dependencies.md)를 참고하세요.

## 공통 버전 규칙

[version.json](../version.json)이 유일한 버전 기준입니다.

| 필드 | 의미 |
|---|---|
| `version` | AU·VST3·Standalone의 공통 버전 |
| `built_at_utc` | 그 변경에 대해 예약한 공통 빌드 시각 |
| `fingerprint` | 두 프로젝트 소스, 빌드 정의와 배포 도구 내용의 SHA-256 |

소스·CMake 정의·공통 버전/설치 스크립트·`packaging/`의 배포 도구와 설치 리소스가 바뀌면 다음 빌드 준비에서 패치 번호가 한 번 증가합니다. 예: `0.1.0 → 0.1.1 → 0.1.2`. 병렬로 AU·VST3를 구성해도 같은 변경에는 같은 번호를 사용합니다.

일반 문서·테스트만 수정한 경우, 변경 없는 빌드·재설치, 같은 소스의 실패 후 재시도에는 번호를 유지합니다. 단, 설치 파일에 포함되는 `packaging/INSTALL.txt`와 `packaging/resources/`는 배포물의 일부이므로 버전 입력에 포함됩니다. 파일 시간만 바뀌고 내용이 같으면 추가 증가하지 않습니다.

버전은 CMake 구성 단계에서 예약되며 빌드가 실패해도 되돌리지 않습니다. 화면과 번들 정보에는 같은 버전·빌드 시각이 반영됩니다. JUCE/AU 버전 필드의 범위를 고려해 패치가 255를 넘으면 마이너로 이월합니다. `version.json`을 삭제하거나 각 CMake 프로젝트에 서로 다른 버전을 하드코딩하지 마세요.

빌드가 `version.json`을 갱신했다면 **그 파일도 소스 변경과 함께 커밋**합니다. 검증된 커밋에 `v0.1.2` 같은 태그를 붙이고 같은 버전의 바이너리를 첨부합니다. README의 버전 요약과 CHANGELOG도 함께 갱신합니다.

## 배포 파일 만들기

[개발 안내](development.md)에 따라 두 프로젝트를 Universal로 구성한 뒤 루트에서 실행합니다.

```sh
python3 scripts/update_local.py --build-only
python3 packaging/build_installer.py
release_version=$(python3 -c 'import json; print(json.load(open("version.json"))["version"])')
python3 packaging/verify_installer.py "dist/SineStack-${release_version}-macOS-Universal-unsigned.pkg" --check-choices
python3 packaging/tests/check_terminal_installer.py "dist/SineStack-${release_version}-macOS-Universal.tar.gz" -v
(cd dist && shasum -a 256 -c SHA256SUMS.txt)
```

`dist/`에 다음 다섯 파일이 생성됩니다. **같은 실행에서 만든 다섯 파일을 한 릴리스에 올립니다.**

| 파일 | 용도 |
|---|---|
| `SineStack-버전-macOS-Universal-unsigned.pkg` | AU·VST3·실행 앱 시스템 전체 설치; 개별 선택 가능 |
| `SineStack-버전-macOS-Universal.tar.gz` | AU·VST3·실행 앱과 매니페스트를 담은 터미널 설치용 아카이브 |
| `install.sh` | macOS 기본 도구로 실행되는 설치·업데이트 도구 |
| `INSTALL.txt` | 한국어·영어 설치 안내 |
| `SHA256SUMS.txt` | 위 네 파일의 SHA-256 |

빌더는 공통 버전·빌드 정보·Universal 구성·최소 OS·서명을 검사합니다. 검증 명령은 패키지의 세 번들 내용과 원본을 비교하고 7가지 선택 조합을 확인하며, 실제 시스템 설치는 하지 않습니다. 터미널 검사는 임시 사용자 폴더에서 설치·재설치·업데이트·설정 보존·손상 거부·실패 복원을 확인합니다. 온라인 다운로드 흐름은 모의 GitHub 응답으로 검사하므로 게시 후 실제 다운로드 확인도 수행하세요.

현재 도구는 ad-hoc 서명된 번들과 **unsigned·미공증** `.pkg`를 생성합니다. Developer ID 서명·Apple 공증을 자동 수행하는 파이프라인은 구현되어 있지 않습니다.

내장 사운드 10개는 이미 번들에 포함됩니다. [Factory Presets](../Factory%20Presets/README.md) ZIP은 선택적 자료이며 앱 ZIP을 따로 만들 필요는 없습니다.

## GitHub Release 만들기

1. 검증한 소스·`version.json`·변경 이력을 커밋하고 원격 저장소에 푸시합니다.
2. 저장소의 **Releases → Draft a new release**를 엽니다.
3. 공통 버전과 같은 태그(예: `v0.1.2`)를 고르고 검증한 커밋을 대상으로 지정합니다.
4. 변경사항·지원 OS·설치 방법·서명 상태를 작성합니다.
5. 위 다섯 파일을 Assets에 첨부하고 버전·체크섬을 확인한 뒤 공개합니다.

기본 설치 명령은 GitHub의 최신 **정식 릴리스**를 조회합니다. 초안은 다운로드할 수 없고 pre-release는 기본 최신 조회 대상에서 제외됩니다. 공개 pre-release를 시험하려면 `--version 0.1.2`처럼 해당 태그의 버전을 명시합니다. 파일명과 `vX.Y.Z` 태그 형식을 유지하세요.

게시 후 아래 명령으로 실제 GitHub 다운로드와 번들 검증을 확인합니다. `--check`는 설치본을 교체하지 않습니다.

```sh
bash packaging/install.sh --repo choi00608/Sine-Stack --check
```

사용자에게 안내할 첫 설치 명령:

```sh
curl -fL --proto '=https' --proto-redir '=https' \
  https://github.com/choi00608/Sine-Stack/releases/latest/download/install.sh -o install.sh && bash install.sh
```

다음 업데이트 명령:

```sh
bash "$HOME/Library/Application Support/SineStack/Installer/install.sh"
```

GitHub의 [릴리스 작성 안내](https://docs.github.com/en/repositories/releasing-projects-on-github/managing-releases-in-a-repository)와 [최신 릴리스 API](https://docs.github.com/en/rest/releases/releases#get-the-latest-release)를 참고하세요.

릴리스 본문 예시:

```text
Sine Stack v0.1.2

변경사항
- AU·VST3·실행 앱 통합 터미널 설치·업데이트
- .pkg에 실행 앱 포함

지원 환경
- macOS 11+, Universal arm64 / x86_64
- AU / VST3 / Standalone 모두 포함

설치
- README의 터미널 명령 또는 .pkg 선택 설치
- 사용자 프리셋·설정 유지, 설치 후 앱과 DAW 재실행
- Developer ID 서명·Apple 공증 없음, 번들은 ad-hoc 서명

검증 및 알려진 제한
- 실행한 검사와 호스트 기록
- 사용자에게 영향을 주는 제한 기록
```

이미 배포한 동일 버전의 바이너리를 바꾸기보다 새 버전으로 배포해 태그·소스·설치 파일이 일치하도록 관리합니다.
