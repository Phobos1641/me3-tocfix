# ME3 TOCFix

## About

I refuse to install .NET/Mono just to use a simple tool, and the pre-built ASI version didn't parse my DLC, and so this project was spawned.

It compiles and works cross platform and is completely open source.

The code should hopefully be reasonably well documented and possible most to read through and understand.

### Usage

If compiled for Windows, the utility should get the path to your game install automagically.
Should that fail, or you're running on another platform, you will need to provide the full path
to the `PCConsoleTOC.bin` file (or where it should be located if missing).

`me3coalfix.exe "C:\Games\Mass Effect 3\BIOGame\PCConsoleTOC.bin"`

```bash
./me3coalfix "/path/to/PCConsoleTOC.bin"
```

## Download

Head over to [Releases](https://gitgud.io/orochi/mods/mass-effect/me3-tocfix/-/releases) and download the latest release matching your platform.

## Credits

* AutoTOC from the ME3Tweaks project.

This project is not affiliated in any way with any of the above mentioned projects other than to acknowledge its source of inspiration.

## License

[GNU Affero General Public License v3.0 only](/LICENSE)
