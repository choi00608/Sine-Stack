# Sine Stack 작업 지침

- AU와 VST3 프로젝트의 공통 소스 변경은 양쪽에 함께 반영합니다.
- 앱을 수정한 뒤에는 필요한 검증을 마치고 **AU, VST3, 설치된 Standalone 앱 세 가지를 모두 최신 빌드로 갱신**합니다. 사용자가 이 갱신을 지속적으로 요청했으므로 매번 다시 확인하지 않습니다.
- 로컬 갱신 명령은 `python3 scripts/update_local.py`입니다. CMake가 PATH에 없으면 `--cmake /private/tmp/au-build-tools/bin/cmake`를 지정합니다. 이 명령은 양쪽 프로젝트를 빌드하고 서명·Universal 구성·Bluetooth 권한 설명을 검사한 뒤 설치합니다.
- 버전은 루트 `version.json` 한 곳에서 관리합니다. 소스·빌드 정의·배포 도구와 설치 리소스의 내용이 바뀌면 CMake 구성 시 패치 버전을 한 번 예약하고 AU·VST3·Standalone에 같은 번호와 빌드 시각을 적용합니다. 변경 없는 빌드·재설치·실패 후 재시도에서는 번호를 유지합니다. 각 프로젝트의 버전을 따로 수정하지 않습니다.
- 설치 전 빌드만 검증하려면 같은 명령에 `--build-only`를 붙입니다. 빌드 중 소스가 바뀌거나 번들의 버전·소스 식별 정보가 다르면 설치가 중단되므로 다시 통합 빌드합니다.
- 설치 위치:
  - `~/Library/Audio/Plug-Ins/Components/Sine Stack.component`
  - `~/Library/Audio/Plug-Ins/VST3/Sine Stack.vst3`
  - `~/Applications/Sine Stack.app` (AU 프로젝트의 Standalone 빌드 사용)
- 샌드박스 밖 설치에는 실행 도구의 권한 상승을 사용합니다. 사용자 설정과 프리셋은 유지하고, 실행 중인 DAW나 앱을 강제로 종료하지 않습니다. 이미 실행 중인 프로세스에는 재실행 후 갱신이 적용됩니다.
- `build-before-move`는 이전 빌드 백업입니다. 이를 설치 원본으로 사용하지 않습니다.
- 공통 사용자 문서는 루트 `README.md`와 `docs/`에서 관리하며 AU·VST3 README에는 중복 설명을 추가하지 않습니다. 화면 자료는 `docs/images/`에 둡니다.
- 새 버전을 배포할 때 루트 README의 버전 요약과 `CHANGELOG.md`를 함께 갱신합니다. 문서·이미지만 수정했다면 앱 버전을 올리거나 설치본을 다시 교체하지 않습니다.
