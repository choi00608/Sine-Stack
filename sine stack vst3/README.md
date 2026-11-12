# Sine Stack VST3

이 폴더는 **VST3 + Standalone** 프로젝트입니다. 전체 소개·설치·사용법은 [루트 README](../README.md)에 있습니다.

- [개발 및 검사](../docs/development.md)
- [공통 버전과 배포](../docs/releases.md)
- [사용 설명서](../docs/user-guide.md)

저장소 루트의 공통 버전 도구를 사용하므로 이 폴더만 분리하지 마세요. 이 폴더에서 빌드할 때는 다음 명령을 사용합니다.

```sh
cmake --preset universal
cmake --build --preset universal
ctest --preset universal
```

산출물은 `build/SineStack_artefacts/Release/VST3/Sine Stack.vst3`와 `build/SineStack_artefacts/Release/Standalone/Sine Stack.app`입니다. 현재 macOS Universal 빌드만 제공하며 Windows용 VST3는 포함하지 않습니다. 이 프로젝트에는 실제 VST3를 로드하는 별도 호스트 검사도 있습니다.

공통 기능은 AU 쪽 Source에도 함께 반영합니다. 최종 갱신은 저장소 루트에서 `python3 scripts/update_local.py`로 AU·VST3·설치된 앱을 함께 수행합니다. 사용자 앱 설치에는 AU 프로젝트의 Standalone을 사용합니다.
