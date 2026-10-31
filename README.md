# ERA-QMK-H7S-FW source

This repository publishes the corresponding source code of each released ERA-QMK-H7S-FW firmware, as the GNU General Public License requires. Each release is one commit tagged with its firmware version, so the repository's tags list the published releases. Development happens elsewhere, so this repository does not take issues or pull requests.

## Build

Requirements: the Arm GNU toolchain (`arm-none-eabi-gcc`), CMake 3.13 or later, Python 3 and `make`. On Windows, set `ARM_TOOLCHAIN_DIR` to the toolchain's `bin` directory and use MinGW make; elsewhere the toolchain must be on `PATH`.

Mac/Linux:

```sh
cmake -S . -B build -DKEYBOARD_PATH='/keyboards/era/keynetix/may65'
cmake --build build -j10
```

Windows (MinGW):

```sh
cmake -S . -B build -G "MinGW Makefiles" -DKEYBOARD_PATH='/keyboards/era/keynetix/may65'
cmake --build build -j10
```

`KEYBOARD_PATH` selects a board under `src/ap/modules/qmk/keyboards/`; Brick60, for example, is `/keyboards/era/sirind/brick60`. Keep the build directory directly under this directory. The build writes the UF2 image `<BOARD>-<version>.uf2` there.

## License

ERA-QMK-H7S-FW is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version. See `LICENSE`. `THIRD_PARTY_NOTICES.md` lists the components it contains and their own copyright and license terms.

It was ported from [baram-qmk-h7s](https://github.com/chcbaram/baram-qmk-h7s) by Hancheol Cho.

## 한국어

이 저장소는 GPL 조건에 따라, 배포한 ERA-QMK-H7S-FW 펌웨어의 소스 코드를 릴리스마다 공개합니다. 릴리스마다 커밋 하나를 올리고 펌웨어 버전으로 태그를 붙이므로, 공개한 버전은 태그 목록에서 확인할 수 있습니다. 개발은 다른 곳에서 하므로 이 저장소는 이슈와 풀 리퀘스트를 받지 않습니다. 빌드 방법과 라이선스는 위의 영어 설명과 같습니다.
