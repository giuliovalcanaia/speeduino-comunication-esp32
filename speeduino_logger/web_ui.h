#pragma once

// Servidor web de logs (somente USE_WIFI = 1).
// Permite listar e baixar os arquivos CSV do cartao SD diretamente pelo
// navegador em http://192.168.4.1/.
// Em modo Bluetooth vira stub (nao faz nada).

void webUiInit();
void webUiHandle();
