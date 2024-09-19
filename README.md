# Projeto 02 - Sistemas em Tempo Real

Acadêmicos: Alexandre Debortoli de Souza e Maria Julia Lamim Severino
Orientador: Felipe Viel

## Sistema de Monitoramento de Veículo

Este projeto foi gerado a partir do exemplo `hello_world` da Espressif. O sistema é desenvolvido para a placa ESP32 e utiliza o FreeRTOS.

## Estrutura do Projeto

Dentro da pasta `main`, você encontrará três arquivos .c:

- `cyclic_executive.c`: Implementação da versão com execução cíclica.
- `interrupt.c`: Implementação da versão que utiliza interrupções.
- `microkernel.c`: Implementação da versão com microkernel.

## Como Rodar

	1. Escolha a versão que deseja executar e modifique o arquivo `CMakeLists.txt` na pasta `main` para incluir o arquivo correspondente. Por exemplo, para rodar a versão do microkernel, você deve alterar a linha idf_component_register da seguinte forma:

  ```
  idf_component_register(SRCS "microkernel.c" INCLUDE_DIRS "")
  ```

  2.	Compile o projeto:

  ```
  idf.py build
  ```

  3.	Faça o upload para a placa:
  ```
  idf.py flash
  ```

  4.	Monitore a saída:
  ```
  idf.py monitor
  ```

Após seguir esses passos, o sistema estará pronto para testes.