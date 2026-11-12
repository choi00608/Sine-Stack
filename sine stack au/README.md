# Sine Stack AU

이 폴더는 **AU + Standalone** 프로젝트입니다. 전체 소개·설치·사용법은 [루트 README](../README.md)에 있습니다.

- [개발 및 검사](../docs/development.md)
- [공통 버전과 배포](../docs/releases.md)
- [사용 설명서](../docs/user-guide.md)

저장소 루트의 공통 버전 도구를 사용하므로 이 폴더만 분리하지 마세요. 이 폴더에서 빌드할 때는 다음 명령을 사용합니다.

```sh
cmake --preset universal
cmake --build --preset universal
ctest --preset universal
```

산출물은 `build/SineStack_artefacts/Release/AU/Sine Stack.component`와 `build/SineStack_artefacts/Release/Standalone/Sine Stack.app`입니다. 통합 설치에는 이 프로젝트의 Standalone을 사용합니다. `build-before-move`는 설치에 사용하지 않는 이전 빌드 백업입니다.

공통 기능은 VST3 쪽 Source에도 함께 반영합니다. 최종 갱신은 저장소 루트에서 `python3 scripts/update_local.py`로 AU·VST3·설치된 앱을 함께 수행합니다.
