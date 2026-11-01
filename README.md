# Gen LRC Lyrics

**A synchronized lyrics plugin for Winamp and WACUP.**

Gen LRC Lyrics is a General Purpose Plugin for Winamp-compatible players, designed to display synchronized lyrics in a dedicated window.

## Features

- Synchronized lyrics display.
- Support for external `.lrc` files.
- Embedded lyrics support for MP3, FLAC, OGG, Opus and M4A.
- Automatic lyrics switching when the playing track changes.
- Support for lyrics without timestamps.
- Adjustable synchronization offset.
- Seek through the song by double-clicking a lyric line.
- Customizable font.
- Always-on-top window option.
- Saves window position and plugin settings.

## Compatibility

- WACUP 32-bit (x86).
- Winamp-compatible General Purpose Plugin architecture.

## Installation

1. Download the latest `Gen-LRC-Lyrics-Setup.exe` from the [Releases](../../releases) page.
2. Run the installer.
3. Select your WACUP installation folder if it is not detected automatically.
4. Complete the installation.
5. Open WACUP and enable Gen LRC Lyrics in the plugin settings, if necessary.

## Building from source

The project is written in C++ using the Win32 API.

To compile it, you need:
- w64devkit with the 32-bit MinGW compiler.
- The required Winamp plugin SDK headers.

Run the provided build script:

```bat
build_w64devkit.bat
```

The compiled plugin is `gen_lrclyrics.dll`.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE).

## AI-Assisted Development

Gen LRC Lyrics was developed with substantial assistance from artificial intelligence.

The initial C++ source code and build scripts were generated with AI assistance. AI was also used throughout the development process for implementation guidance, code adjustments, troubleshooting and documentation.

The project creator was responsible for defining the project goals, directing development, compiling and integrating the files, performing practical tests, validating functionality, creating the visual assets, configuring the installer and publishing the project.

AI was used as a development tool, while the project creator managed and carried out the development process.

---

# Gen LRC Lyrics (Português)

**Um plugin de letras sincronizadas para Winamp e WACUP.**

O Gen LRC Lyrics é um General Purpose Plugin para players compatíveis com Winamp, desenvolvido para exibir letras sincronizadas em uma janela própria.

## Recursos

- Exibição de letras sincronizadas.
- Suporte a arquivos `.lrc` externos.
- Leitura de letras incorporadas em MP3, FLAC, OGG, Opus e M4A.
- Troca automática de letras conforme a música reproduzida.
- Suporte a letras sem marcações de tempo.
- Ajuste manual de sincronização.
- Navegação pela música ao clicar duas vezes em uma linha da letra.
- Personalização da fonte.
- Opção de manter a janela sempre visível.
- Salvamento da posição da janela e das configurações.

## Compatibilidade

- WACUP 32-bit (x86).
- Arquitetura de plugins General Purpose compatível com Winamp.

## Instalação

1. Baixe a versão mais recente do `Gen-LRC-Lyrics-Setup.exe` na página de [Releases](../../releases).
2. Execute o instalador.
3. Se a pasta do WACUP não for detectada automaticamente, selecione-a manualmente.
4. Conclua a instalação.
5. Abra o WACUP e habilite o Gen LRC Lyrics nas configurações de plugins, caso necessário.

## Compilação

O projeto foi desenvolvido em C++ utilizando a Win32 API.

Para compilar, você precisará de:
- w64devkit com o compilador MinGW de 32 bits.
- Os cabeçalhos necessários do Winamp Plugin SDK.

Execute o script:

```bat
build_w64devkit.bat
```

O resultado da compilação será `gen_lrclyrics.dll`.

## Licença

Este projeto está sob a licença MIT. Consulte o arquivo [LICENSE](LICENSE).
