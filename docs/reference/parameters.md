# 파라미터와 동작 참고

[문서 목록](../README.md) · [사용 설명서](../user-guide.md)

아래 값은 현재 코드 정의를 기준으로 합니다. 전체 파라미터 ID·기본값은 [SourceSettings.h](../../sine%20stack%20au/Source/SourceSettings.h), [FxDetails.h](../../sine%20stack%20au/Source/FxDetails.h), [EqSettings.h](../../sine%20stack%20au/Source/EqSettings.h), [Plugin.cpp](../../sine%20stack%20au/Source/Plugin.cpp)에 있습니다.

## 기본 연주와 소스

| 영역 | 조절 범위·선택 |
|---|---|
| ADSR | Attack 5 ms~2 s, Decay 5 ms~4 s, Sustain 0~100%, Release 20 ms~4 s |
| 연주 | 8음 Poly, Mono Retrigger / Legato, MIDI velocity, 피치 벤드 ±2반음 |
| Oscillator | Sine / Square / Saw, Level 0~100%, Transpose ±24반음, Fine ±100 cents, Pan |
| Sub Oscillator | Level, -1 / -2 / -3옥타브, Sine / Square / Saw, Fine ±50 cents, Pan |
| Noise | Level, Colour -100~100%, Colour Freq 100 Hz~16 kHz, Width, Pan |
| Unison / Detune | 1 / 3 / 5 voices, Detune 0~50 cents, Mix, Width, Pan |
| Harmonizer | Off / 단3도 / 장3도 / 5도 / 단3도+5도 / 장3도+5도, Mix, Transpose ±24반음, Fine ±50 cents, Width, Pan |
| Glide | Time 0~2 s, Curve 0.25~4, Amount |

사각파·톱니파에는 PolyBLEP 보정을 적용합니다. 서브와 노이즈를 포함한 소스는 MIDI velocity와 고정 ADSR을 따르며 각자의 체인 위치에서 소리를 더합니다.

유니즌은 입력 오디오에 디튠된 피치 변환 사본을 더합니다. 1 voice 또는 Detune 0이면 입력을 그대로 통과합니다. 하모나이저는 입력의 변환음을 선택한 간격으로 더합니다. 여러 피치 FX는 직렬로 처리됩니다.

Glide는 같은 MIDI 채널의 직전 음을 기준으로 입력 오디오 전체의 피치를 이동합니다. Poly에서도 개별 보이스별 글라이드가 아니라 앞쪽에서 합쳐진 소리를 마지막 MIDI 음 기준으로 처리합니다. 첫 음은 바로 시작하며 설정 변경은 다음 노트에 적용됩니다.

## FX 상세 설정

| FX | 설정 |
|---|---|
| 공통 | Amount, Output -24~+12 dB; 바이패스는 Output 게인까지 제외 |
| Drive | 강도·Tone, Soft / Hard / Arc, Bias -100~100% |
| Chorus | Rate·Depth, Centre Delay 1~40 ms, Feedback -90~90% |
| Delay | Free Time 10~2000 ms, Feedback, Low Cut Off~2 kHz, High Cut 200 Hz~Off, Stereo / Ping-pong, Tempo Sync |
| Reverb | Room Size·Damping, Pre-delay 0~200 ms, Width 0~100%, Low Cut Off~2 kHz |
| Filter | Low-pass / High-pass / Band-pass, Cutoff 20 Hz~20 kHz, Resonance, Drive 0~24 dB, 12 / 24 dB/oct |
| Tremolo | Rate 0.05~20 Hz, Depth, Sine / Triangle / Square, Stereo Phase 0~180° |
| Phaser | Rate 0.05~10 Hz, Depth, Centre 20 Hz~20 kHz, Feedback -90~90% |
| EQ | 세 밴드 모두 벨 타입; 각각 Frequency 20 Hz~20 kHz, Gain ±18 dB, Q 0.2~8 |

필터 주파수는 실제 샘플레이트의 안전한 범위로 제한합니다. Delay Sync는 최대 8초까지 지원하며 더 긴 박자는 8초로 제한됩니다. Free 모드의 10 ms보다 짧은 동기화 박자도 지원합니다. 시간·박자·템포 변경 시 딜레이 버퍼를 비우지 않고 시간 스무딩을 적용하므로 반복음의 피치가 일시적으로 변할 수 있습니다.

## LFO와 연결 계산

LFO는 Sine / Triangle / Saw up / Saw down / Square, Free Rate 0.05~20 Hz, Depth, Phase 0~360°, Offset을 제공합니다. 직접 소스 Pitch·Volume, 전체 Filter Cutoff, 개별 블록 파라미터를 변조할 수 있습니다.

- Pitch Range는 0~24반음, Filter Range는 0~8옥타브입니다.
- 개별 연결의 깊이는 -100~100%이고, 실제 양은 Master Depth × 연결 Depth × LFO 파형입니다.
- 같은 파라미터의 여러 변조값을 더한 뒤 허용 범위로 제한합니다. 노브의 비선형 이동 곡선도 따릅니다.
- 직접 소스 음량 변조는 소스에 적용되며 이미 생성된 딜레이·리버브 잔향을 직접 감쇠시키지 않습니다.
- LFO 간 연결은 최대 32샘플 제어 구간의 이전 상태를 사용합니다. 위치 순서에 의존하거나 즉시 재귀 계산하지 않습니다.
- 건반 입력에 의한 LFO 재트리거는 현재 제공하지 않습니다.

## 모노, 잔향과 지연

Mono Retrigger는 새 음에서 이전 음을 짧게 페이드아웃하고 ADSR·오실레이터 위상을 재시작합니다. Mono Legato는 겹친 노트에서 현재 엔벌로프·위상·첫 음의 velocity를 유지합니다. 마지막으로 누른 음이 우선하며 놓으면 아직 유지된 이전 음으로 돌아갑니다. 서스테인·소스테누토 페달도 반영합니다.

피치 변환은 Signalsmith Stretch를 사용합니다. Live는 약 12 ms, Detail은 약 48 ms의 변환음 지연이 단계별로 붙습니다. 예를 들어 변환음 직렬 4단은 각각 약 48 / 192 ms입니다. 원음·바이패스 경로와 LFO는 이 피치 변환 지연을 추가하지 않습니다. 원음과 변환음을 섞으므로 플러그인 전체에 하나의 고정 지연 보정을 적용하지 않습니다.

## 상태와 호환성

사운드 프리셋의 `formatVersion`과 앱 버전은 별개입니다. 현재 `.sinestack`의 formatVersion 1·2를 읽으며, 명시적 체인은 formatVersion 2에 순서와 인스턴스 ID를 저장합니다. 카테고리는 선택적 메타데이터이며 없는 파일은 Uncategorized로 처리합니다.

블록 종류·세부 파라미터·바이패스·순서·LFO 연결·Voice Mode·Pitch Latency는 프리셋과 DAW 상태에 포함됩니다. 연주 중 노트, 현재 위상, 딜레이·리버브 버퍼는 파일이나 복제본에 포함하지 않습니다. 샘플레이트와 디자인 테마는 사운드 프리셋에 포함하지 않습니다.

이전 체인에 있던 LFO는 기존 ID·설정·대상 연결을 유지하면서 MODULATION에 표시합니다. 이전 단일 LFO 연결은 첫 번째 연결로 복원합니다. 오래된 합성 단계 기반 프리셋은 현재의 실제 오디오 체인 처리와 음색이 달라질 수 있습니다.

소스·효과의 재배치는 잔향 상태를 유지하지만 신호 경로를 바꿉니다. 효과 종류를 바꾸면 해당 슬롯의 잔향이 초기화될 수 있습니다. 노브는 자동화·프리셋의 기준값을 유지하고 변조 결과를 저장값으로 덮어쓰지 않습니다.
