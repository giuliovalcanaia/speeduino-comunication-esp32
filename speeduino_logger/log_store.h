#pragma once

// Armazenamento de logs no cartao SD.
//   - Um arquivo por sessao/dia/rotacao, em pastas diarias (/YYYY-MM-DD/)
//   - Rotacao automatica a 10 MB ou na virada do dia
//   - Limpeza automatica dos logs mais antigos abaixo de SD_FREE_MARGIN_MB
//   - Fallback /NO_RTC/ quando o RTC falha
//
// Todo acesso ao SD fora deste modulo deve ser protegido por
// logStoreLock() / logStoreUnlock().

#include <Arduino.h>
#include "frame_queue.h"

// Inicializa o barramento SPI, monta o SD e abre o primeiro arquivo de log.
// false = SD indisponivel; o logger continua tentando em background via
// logStoreEnsureOpen().
bool logStoreInit();

// Chamada periodica pela task de SD: reabre o log se o SD falhou.
void logStoreEnsureOpen();

// Converte o frame em linha de CSV e grava no arquivo aberto, fazendo
// flush/rotacao/cleanup conforme necessario.
void logStoreWriteFrame(const LogFrame& frame);

// Fecha o log atual e abre um novo imediatamente (util apos ajuste do RTC).
// Retorna true se o novo log foi aberto com sucesso.
bool logStoreRotate();

// true = SD operante (pode gravar)
bool logStoreIsOk();

// Caminho completo do arquivo que esta sendo gravado ("" se nenhum).
String logStoreCurrentPath();

// Mutex do barramento SD — obrigatorio para qualquer acesso direto ao SD
// fora deste modulo (ex.: servidor web). Nao chamar funcoes logStore*
// com o mutex ja tomado (deadlock).
void logStoreLock();
void logStoreUnlock();
