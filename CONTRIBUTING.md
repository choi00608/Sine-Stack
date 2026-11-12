# 수정과 문제 보고

[프로젝트 소개](README.md) · [개발·빌드](docs/development.md) · [버전·배포](docs/releases.md)

## 기능 수정

AU·VST3의 공통 Source는 별도 복사본이므로 같은 변경을 양쪽에 반영합니다. 설치본까지 확인하려면 통합 명령으로 AU·VST3·Standalone을 함께 갱신합니다.

기존 파라미터 ID, `.sinestack` 파일, DAW 프로젝트 상태와의 호환성을 확인하세요. 사운드 처리·UI·프리셋 등 변경한 영역에 맞는 검사를 실행하고 실제 수행한 결과만 기록합니다. Universal 빌드 여부와 실제 Intel 하드웨어 실행 여부는 구분합니다.

공통 버전은 자동 관리됩니다. 소스 변경으로 `version.json`이 갱신되면 해당 파일을 함께 커밋합니다. 문서나 테스트만 수정할 때는 버전 번호를 수동으로 올리지 않습니다.

## 문서 수정

빠른 시작은 루트 README, 자세한 설명은 `docs/`에 둡니다. UI의 실제 영문 버튼 이름을 사용하고, 설치 패키지와 실행 앱의 포함 여부를 구분해 적습니다. 화면 이미지는 `docs/images/`에서 관리합니다.

저장소 내부 링크는 상대 경로로 작성합니다. 프로젝트 폴더 이름에 공백이 있으므로 Markdown 링크에서는 `%20`, 셸 명령에서는 따옴표를 사용합니다. 새로운 버전의 릴리스를 준비할 때는 README의 버전 요약과 CHANGELOG도 함께 확인합니다.

## GitHub 저장소에 수정 반영하기

프로젝트 루트에서 변경 내용을 확인하고, 올릴 파일을 지정해 커밋한 뒤 푸시합니다. 예를 들어 README만 수정했다면:

```sh
git status --short
git diff
git add README.md
git diff --cached
git commit -m "Update README"
git push origin main
```

여러 파일을 수정했으면 `git add` 뒤에 해당 경로를 나열합니다. 푸시가 원격의 새 커밋 때문에 거부되면 작업을 커밋한 상태에서 `git pull --rebase origin main`으로 통합하고 충돌을 해결한 뒤 다시 푸시합니다. 강제 푸시는 사용하지 않습니다.

`.gitignore`는 아직 추적하지 않는 파일을 제외하는 규칙입니다. 이미 올라간 파일은 규칙을 추가해도 자동으로 제거되지 않습니다. 로컬 파일은 남기고 저장소에서만 제외하려면 다음처럼 추적을 해제하고 함께 커밋합니다.

```sh
git rm --cached -- AGENTS.md
git add .gitignore
git diff --cached
git commit -m "Stop tracking local agent instructions"
git push origin main
```

위 추적 해제는 해당 파일이 아직 Git에 등록되어 있을 때 한 번만 수행합니다. `AGENTS.md`·`agent.md`, 로컬 에이전트 설정, 빌드·배포 산출물, 개인 환경 설정은 `.gitignore`로 제외합니다. 내장 프리셋·예시 오디오·문서 이미지는 공개 자료이므로 계속 추적합니다.

확인은 `git ls-files -- AGENTS.md`에 출력이 없고 `git check-ignore -v AGENTS.md`에 제외 규칙이 표시되는지 보면 됩니다. 이전 커밋과 기존 릴리스 태그에는 과거 파일이 남습니다. 이 절차는 Git 이력을 다시 쓰지 않습니다.

문서나 `.gitignore`만 바꾼 경우에는 커밋·푸시로 끝납니다. 앱 수정은 공통 빌드와 검증 후 소스·`version.json`을 커밋·푸시하고, [배포 안내](docs/releases.md)에 따라 새 버전의 Release 파일도 게시해야 설치 명령에서 업데이트를 받을 수 있습니다.

## 문제 보고

GitHub 저장소의 Issues를 사용할 경우 다음 내용을 함께 적어 주세요.

- 화면 아래에 표시된 버전과 빌드 시각
- macOS 버전, Apple Silicon / Intel 여부
- Standalone / AU / VST3 중 사용 형식과 DAW 이름·버전
- 문제를 재현하는 순서, 기대한 결과와 실제 결과
- 사용한 블록·프리셋, 관련 화면 또는 필요한 충돌 기록

특정 소리에서만 발생한다면 재현에 필요한 최소 프리셋을 첨부하면 도움이 됩니다. 공유 전 개인 경로나 공개하고 싶지 않은 내용이 포함되어 있지 않은지 확인하세요.
