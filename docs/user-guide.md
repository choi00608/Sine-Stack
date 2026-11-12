# 사용 설명서

[문서 목록](README.md) · [설치](installation.md) · [파라미터 참고](reference/parameters.md)

## 화면 구성

| 위치 | 역할 |
|---|---|
| 상단 | DESIGN, 프리셋 뱅크·카테고리·목록, 저장·불러오기, Output |
| 왼쪽 | 고정 ADSR 그래프·노브, Voice Mode |
| 오른쪽 | 현재 선택한 블록의 종류·세부 설정·바이패스·복제·삭제 |
| AUDIO CHAIN | 소스와 오디오 효과를 추가·선택·재배치하는 횡스크롤 체인 |
| MODULATION | 오디오 체인과 독립적인 LFO 영역 |
| 하단 | 연주용 화면 건반, 현재 버전과 빌드 시각 |

## 첫 소리와 프리셋

**Factory**를 선택하고 목록 또는 **< / >**로 내장 소리 10개를 들어 봅니다. **All categories**에서 Bass, Lead, Pad, Keys, Pluck, Bell, Rhythm, Atmosphere, Motion, FX, Other, Uncategorized를 선택할 수 있습니다. 해당 분류에 프리셋이 없으면 목록은 비어 있습니다.

뱅크나 카테고리만 바꾸면 현재 소리는 유지됩니다. 목록의 항목이나 이전·다음 버튼을 눌러야 소리를 불러옵니다. 이전·다음은 현재 필터에 맞는 프리셋 안에서 순환합니다.

수정한 소리는 **Save Preset → Category → Continue**에서 카테고리를 고르고 이름·위치를 지정해 저장합니다. Factory 원본은 바뀌지 않고 User 사본이 생깁니다.

**Load Preset**은 `.sinestack` 파일을 엽니다. User 목록은 마지막으로 저장하거나 불러온 파일과 같은 폴더를 보여 줍니다. 다른 폴더를 보고 싶으면 그 폴더의 프리셋을 한 번 불러오세요. **Refresh**는 외부에서 추가·삭제·수정한 파일을 반영합니다. 기존 카테고리 없는 파일은 Uncategorized에 표시되며 다시 저장하면서 분류를 지정할 수 있습니다.

파일 내용은 UTF-8 JSON입니다. 확장자를 `.txt`로 바꾸면 텍스트 편집기로 읽을 수 있으며, 앱에서 다시 불러오려면 `.sinestack`으로 되돌립니다. 저장 대상은 설정과 연결이며 현재 울리는 노트·FX 잔향 버퍼는 포함하지 않습니다.

[기본 프리셋별 특징](../Factory%20Presets/README.md)

## ADSR과 연주 모드

Attack은 소리가 커지는 시간, Decay는 최대 음량에서 Sustain까지 줄어드는 시간, Sustain은 누르고 있는 동안의 유지 음량, Release는 건반을 놓은 뒤 줄어드는 시간입니다.

노브와 그래프는 서로 연동됩니다. 그래프의 점은 다음처럼 움직입니다.

| 점 | 방향 | 조절값 |
|---|---|---|
| 첫 번째 | 좌우 | Attack |
| 두 번째 | 좌우 | Decay |
| 세 번째 | 위아래 | Sustain |
| 마지막 | 좌우 | Release |

HELD는 건반을 누르고 있는 구간을 상징적으로 보여 줍니다. NOTE OFF부터 Release가 시작됩니다. 드래그를 마치면 시간 축이 화면에 맞춰 다시 조정됩니다.

**Voice Mode**는 연주에 맞게 선택합니다.

- **Poly (8 voices)**: 코드 등 여러 음을 함께 연주합니다.
- **Mono (Retrigger)**: 마지막으로 누른 한 음을 우선하며 새 음마다 ADSR을 다시 시작합니다.
- **Mono (Legato)**: 노트가 겹치면 현재 ADSR과 오실레이터 위상을 유지하면서 음정을 이어갑니다. 음이 끊긴 후 다시 누르면 Attack부터 시작합니다.

단음 베이스·리드에서 노트 겹침에 따른 출렁임을 줄이고 싶다면 Mono를 사용해 보세요. Mono도 딜레이·리버브 잔향과 피치 FX가 만든 음층은 유지합니다. Glide를 함께 사용하면 음정 이동 시간을 조절할 수 있습니다.

## 오디오 체인 만들기

**+ Add**로 블록을 추가하고 블록을 클릭하면 오른쪽에서 설정합니다. 소스와 효과는 체인의 왼쪽에서 오른쪽으로 처리됩니다.

```text
Oscillator → Drive → Harmonizer → Noise → Filter → Output
```

이 구성에서는 드라이브가 걸린 소리가 하모나이저에 들어갑니다. Noise는 그 뒤에서 더해지므로 앞의 Drive·Harmonizer를 거치지 않고 Filter부터 통과합니다. 소스 블록이 하나도 없으면 새 소리가 생성되지 않습니다.

| 작업 | 조작 |
|---|---|
| 순서 변경 | 오디오 블록을 드래그해서 원하는 위치에 놓기 |
| 바이패스 | 블록의 ON / BYPASSED 또는 오른쪽 설정 패널에서 전환 |
| 복제 | 블록 선택 후 Duplicate |
| 삭제 | 블록의 x 또는 설정 패널의 Remove |
| 드래그 삭제 | 오디오 블록을 패널 밖으로 끌고 Release to remove 표시에서 놓기 |
| 드래그 삭제 취소 | 다시 패널 안으로 가져오거나 Esc |
| 긴 체인 탐색 | 가로 스크롤, 드래그 중 양 끝에서 자동 스크롤 |

AUDIO CHAIN과 MODULATION을 합쳐 최대 32개 블록을 사용합니다. 같은 종류도 여러 번 추가할 수 있습니다. Duplicate는 설정·바이패스·LFO 연결을 복사하지만 현재 위상이나 잔향 버퍼는 복사하지 않습니다.

바이패스는 설정을 보존합니다. 블록을 삭제하면 그 블록으로 향하던 LFO 연결도 해제됩니다. LFO는 MODULATION의 **x**로 삭제하며 오디오 체인 순서에는 포함되지 않습니다.

## 효과 조절

오디오 효과의 **Amount**는 효과 양을, **Output**은 다음 블록에 보내는 레벨을 조절합니다. 블록을 바이패스하면 Output 게인도 제외합니다. 효과마다 주요 조절값이 다릅니다.

| 블록 | 주로 조절할 값 |
|---|---|
| Drive | 왜곡 강도, Tone, Distortion, Bias |
| Chorus | Rate, Depth, Centre Delay, Feedback |
| Delay | Time / Beat, Feedback, Low Cut, High Cut, Stereo / Ping-pong |
| Reverb | Room Size, Damping, Pre-delay, Width, Low Cut |
| Filter | Low-pass / High-pass / Band-pass, Cutoff, Reso, Drive, Slope |
| Tremolo | Rate, Depth, Shape, Stereo Phase |
| Phaser | Rate, Depth, Centre, Feedback |
| EQ | Low / Mid / High의 Frequency, Gain, Q |
| Unison / Detune | Voices, Detune, Mix, Width, Pan |
| Harmonizer | Interval, Mix, Transpose, Fine, Width, Pan |
| Glide | Time, Curve, Amount |

유니즌·하모나이저·글라이드는 앞에서 만들어진 **오디오**를 처리합니다. 여러 개를 직렬로 쌓으면 앞 블록의 변환음과 잔향도 다음 블록의 입력이 됩니다.

### EQ 그래프

EQ의 L / M / H 점을 좌우로 움직이면 주파수, 위아래로 움직이면 게인이 바뀝니다. 점을 선택하면 그 밴드의 노브로 전환됩니다. Q는 노브로 조절합니다. 노브·자동화·프리셋 변경도 그래프에 반영됩니다.

밝은 선은 Amount와 Output을 포함한 합산 응답, 옅은 선은 각 밴드 응답입니다. 세 밴드는 모두 벨 형태입니다. 그래프는 기본 설정값의 응답을 표시하며 LFO의 실시간 움직임은 그리지 않습니다.

## LFO와 여러 대상 연결

1. **MODULATION → + LFO**로 LFO를 추가합니다.
2. LFO 블록을 선택해 파형·속도·Master Depth를 정합니다.
3. 대상 블록과 파라미터를 선택합니다. 예: `Delay → Delay Time`, `Unison / Detune → Detune`.
4. **+ Target**으로 연결을 추가합니다. 하나의 LFO에 최대 8개 연결을 둘 수 있습니다.
5. 연결마다 **Depth**를 조절합니다. 음수는 변조 방향을 뒤집습니다.

연결 깊이는 노브의 전체 이동 범위에 대한 비율입니다. Master Depth 50%와 연결 Depth 20%를 조합하면 기본값에서 노브 범위의 ±10%만큼 움직입니다. 기준 노브 값은 유지되며 변조 결과로 덮어쓰지 않습니다.

대상 블록의 앞뒤 위치와 관계없이 연결되며, 블록을 이동해도 연결을 유지합니다. 다른 LFO도 대상으로 지정할 수 있으나 자기 자신은 제외합니다. 파형·종류·바이패스 같은 선택형 스위치는 대상 목록에서 제외합니다.

### 템포 동기화

LFO 또는 Delay에서 **CLOCK → Tempo Sync**, **BEAT**에서 음표를 고릅니다. 1/1~1/64와 각 길이의 **Dotted**, **Triplet**을 지원합니다.

| 120 BPM 예시 | 시간 | LFO 주기 |
|---|---|---|
| 1/8 | 250 ms | 4 Hz |
| 1/8 Dotted | 375 ms | 약 2.67 Hz |
| 1/8 Triplet | 약 166.67 ms | 6 Hz |

LFO는 선택한 길이마다 한 주기를 반복합니다. 재생 중 호스트 위치에 위상을 맞추며 Phase로 위치를 조절합니다. Delay는 선택한 길이만큼 반복음을 지연시킵니다.

동기화 중 LFO의 Hz Rate와 Delay의 ms Time 노브는 사용하지 않습니다. 그 노브를 향하는 LFO 변조도 적용하지 않지만 연결은 보존합니다. Free 모드로 돌아가면 기존 값을 사용합니다. Delay의 Feedback·Amount 변조는 Sync에서도 사용할 수 있습니다.

호스트 템포가 없으면 기본 120 BPM 또는 마지막 유효 템포를 사용합니다. Standalone에는 별도의 DAW 템포 입력이 없습니다.

## 지연, 출력과 테마

**PITCH LATENCY → Live / 12 ms**는 피치 변환의 반응 속도를, **Detail / 48 ms**는 저음과 촘촘한 화음의 질감을 우선합니다. 변환음이 통과하는 피치 FX 단계마다 지연이 누적됩니다. 이 수치는 모든 블록이나 LFO에 붙는 지연이 아니며 오디오 장치 버퍼 지연은 별도입니다.

여러 소스와 효과를 강하게 쌓으면 출력이 커지거나 클리핑이 생길 수 있습니다. 블록별 Output과 전체 Output으로 음량을 맞추세요. 연주 중 순서·효과 종류·딜레이 시간을 바꾸면 소리가 급변하거나 반복음의 피치가 변할 수 있습니다.

**DESIGN**에서 Synthwave, Classic Green, Arctic Ice, Solar Flare, Ocean Depths, Rose Quartz, Graphite를 고릅니다. 테마는 사운드 프리셋과 독립적으로 기억하며 새로 여는 인스턴스에 적용됩니다. 이미 열린 다른 인스턴스의 테마는 유지됩니다.

건반 아래 오른쪽의 버전과 시각은 **현재 실행 중인 빌드**입니다. 시각은 컴퓨터의 현지 시간대이고 마우스를 올리면 UTC 원본도 표시됩니다. 업데이트 직후라면 앱·DAW를 완전히 다시 열어 확인하세요.
