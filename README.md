# Projeto Exemplo com FreeRTOS em STM32F103C8 (Blue Pill)

## Visão Geral

Este projeto é uma demonstração prática da implementação do sistema operacional de tempo real (RTOS) FreeRTOS em um microcontrolador STM32F103C8, popularmente conhecido como "Blue Pill". O objetivo é apresentar um exemplo funcional que sirva como base para estudos e desenvolvimento de sistemas embarcados mais complexos, além de destacar habilidades técnicas essenciais para a área de sistemas embarcados.

Este repositório é ideal para recrutadores que buscam candidatos com experiência sólida em programação de microcontroladores, conhecimento de RTOS e familiaridade com ferramentas de desenvolvimento profissionais.

## Destaques Técnicos e Habilidades Demonstradas

  * **Sistema Operacional de Tempo Real (RTOS):** Utilização do FreeRTOS para gerenciamento de tarefas, demonstrando proficiência em conceitos de multitarefa, escalonamento e comunicação inter-task em um ambiente com recursos limitados.
  * **Arquitetura de Microcontrolador:** Desenvolvimento para a arquitetura ARM Cortex-M3, uma das mais difundidas no mercado de sistemas embarcados. O projeto é configurado para o microcontrolador STM32F103C8.
  * **Ambiente de Desenvolvimento Profissional (IDE):** O projeto foi desenvolvido utilizando o Keil uVision, um ambiente de desenvolvimento integrado padrão da indústria para microcontroladores ARM.
  * **Toolchain e Compilador:** Utilização do toolchain ARM-ADS (ARM Development Studio), demonstrando familiaridade com compiladores e linkers específicos para sistemas embarcados.
  * **Configuração de Hardware e Memória:** O projeto inclui a configuração detalhada do mapa de memória do microcontrolador, alocando 20KB para IRAM e 64KB para IROM.
  * **Depuração Avançada (Debugging):** O arquivo de configuração de depuração (`.dbgconf`) foi ajustado para permitir o debug do sistema mesmo em modos de baixo consumo de energia (Sleep, Stop e Standby). Isso demonstra uma compreensão aprofundada dos desafios de depuração em sistemas embarcados.

## Hardware e Software

### Hardware

  * Placa de desenvolvimento baseada no STM32F103C8 (ex: Blue Pill).
  * Depurador/Gravador ST-Link V2 ou similar.

### Software

  * [Keil uVision IDE](https://www.google.com/search?q=https://www.keil.com/demo/eval/arm.htm)
  * [Keil.STM32F1xx\_DFP (Device Family Pack)](https://www.google.com/search?q=https://www.keil.com/dd2/pack/keil.stm32f1xx_dfp)
  * Toolchain ARM-ADS

## Como Compilar e Utilizar

1.  Clone este repositório para a sua máquina local.
2.  Abra o arquivo de projeto `ex.uvprojx` no Keil uVision.
3.  Conecte a sua placa STM32 e o depurador ST-Link.
4.  Compile o projeto clicando em "Project" -\> "Build Target".
5.  Faça o download do código para o microcontrolador clicando em "Flash" -\> "Download".
6.  Inicie uma sessão de depuração clicando em "Debug" -\> "Start/Stop Debug Session".

## Análise do Projeto

Este projeto, embora simples em sua funcionalidade final, encapsula uma série de conhecimentos técnicos que são cruciais para um engenheiro de sistemas embarcados. A estrutura de arquivos do Keil uVision (`.uvprojx`, `.uvoptx`, `.uvguix.LNV`) foi versionada para garantir a reprodutibilidade do ambiente de desenvolvimento.

A configuração do debugger para manter o sistema ativo durante modos de baixo consumo é um diferencial que demonstra atenção aos detalhes e experiência prática na resolução de problemas complexos de depuração.
