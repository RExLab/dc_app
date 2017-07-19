#include "timer/timer.h"
#include "_config_cpu_.h"
#include "uart/uart.h"
#include "modbus/modbus_master.h"
#include <nan.h>
#include "app.h"
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <v8.h>
#include <string.h>

#define SLEEP_TIME 100

using namespace v8;
static  pthread_t id_thread;
static int waitResponse = pdTRUE;
static tCommand cmd;
static u16 regs[120]; // registrador de trabalho para troca de dados com os multimetros

tControl control;

int modbus_Init(void) {
	//fprintf(flog, "Abrindo UART %s"CMD_TERMINATOR, COM_PORT);
	#if (LOG_MODBUS == pdON)
	printf("Abrindo UART %s"CMD_TERMINATOR, COM_PORT);
	#endif
	if (uart_Init(COM_PORT, MODBUS_BAUDRATE) == pdFAIL ) {
   		//fprintf(flog, "Erro ao abrir a porta UART"CMD_TERMINATOR);
		//fprintf(flog, "    Verifique se essa porta não esteja sendo usada por outro programa,"CMD_TERMINATOR);
		//fprintf(flog, "    ou se o usuário tem permissão para usar essa porta: chmod a+rw %s"CMD_TERMINATOR, COM_PORT);
   		#if (LOG_MODBUS == pdON)
   		printf("Erro ao abrir a porta UART"CMD_TERMINATOR);
		printf("    Verifique se essa porta não esteja sendo usada por outro programa,"CMD_TERMINATOR);
		printf("    ou se o usuário tem permissão para usar essa porta: chmod a+rw %s"CMD_TERMINATOR, COM_PORT);
		#endif
		return pdFAIL;
	}

	//fprintf(flog, "Port UART %s aberto com sucesso"CMD_TERMINATOR, COM_PORT);
	#if (LOG_MODBUS == pdON)
	printf("Port UART %s aberto com sucesso a %d bps"CMD_TERMINATOR, COM_PORT, MODBUS_BAUDRATE);
	#endif
	modbus_MasterInit(uart_SendBuffer, uart_GetChar, uart_ClearBufferRx);
	modbus_MasterAppendTime(now, 3000);

	return pdPASS;
}



// para gravação os valores dos registradores devem estar preenchidos
// após a leitura os registradores estarão preenchidos

// Envia um comando para o escravo, nesta mesma função é feito o procedimento de erro de envio
// Uma vez enviado o comando com sucesso caprturar resposta no modbus_Process
//	BUSY: Ficar na espera da resposta
//  ERROR: Notificar  o erro e tomar procedimento cabíveis
//  OK para escrita: Nada, pois os valores dos registradores foram salvos no escravo com sucesso
//	OK para Leitura: Capturar os valores dos registradores lidos do escravo
// Parametros
// c: Tipo de comando para ser enviado ao escravo
// funcResponse: Ponteiro da função para processar a resposta da comunicação


void modbus_SendCommand( tCommand c) {
    if (waitResponse) return ;// pdFAIL;

	cmd = c;
    // APONTA QUAIS REGISTRADORES A ACESSAR NO DISPOSITIVO
    // -----------------------------------------------------------------------------------------------------------------

    // Comando para ler os registradores: modelo, versão firmware e modo de operações dos reles
    tcmd typeCMD = readREGS;
    uint addrInit = 0;
    uint nRegs = 3;
	u16 value = 0;

    // comando para ler os estados das saidas digitais
    if (cmd == cmdGET_DOUTS) {
        addrInit = 0x200;
        nRegs = 1;

    // comando para gravar os estados das saidas digitais
    } else if (cmd == cmdSET_DOUTS) {
        typeCMD = writeREG;
        addrInit = 0x200;
        nRegs = 1;
        value = control.douts;

	// comando para ler os estados dos reles
    } else if (cmd == cmdGET_RELAYS) {
        addrInit = 0x300;
        nRegs= 1;

    // comando para gravar os estados dos reles
    } else if (cmd == cmdSET_RELAYS) {
        typeCMD = writeREG;
        addrInit = 0x300;
        nRegs = 1;
        value = control.relays;

    // Comando para ler os valores dos sensores
    } else if (cmd == cmdGET_MULTIMETERS) {
        addrInit = 0x400;
        nRegs = control.nMultimetersGeren*4;
    }

   	// ENVIA O COMANDO AO DISPOSITIVO ESCRAVO
   	// -----------------------------------------------------------------------------------------------------------------
   	int ret;
    if (typeCMD == writeREG) {
    	#if (LOG_MODBUS == pdON)
    	//fprintf(flog, "modbus WriteReg [cmd %d] [slave %d] [reg 0x%x] [value 0x%x]"CMD_TERMINATOR, cmd, control.rhID, addrInit, value);
    	printf("modbus WriteReg [cmd %d] [slave %d] [reg 0x%x] [value 0x%x]"CMD_TERMINATOR, cmd, control.rhID, addrInit, value);
    	#endif
		ret =  modbus_MasterWriteRegister(control.rhID, addrInit, value);
	} else if (typeCMD == writeREGS) {
	   	#if (LOG_MODBUS == pdON)
    	//fprintf(flog, "modbus WriteRegs [cmd %d] [slave %d] [reg 0x%x] [len %d]"CMD_TERMINATOR, cmd, control.rhID, addrInit, nRegs);
    	printf("modbus WriteRegs [cmd %d] [slave %d] [reg 0x%x] [len %d]"CMD_TERMINATOR, cmd, control.rhID, addrInit, nRegs);
    	#endif
        ret = modbus_MasterWriteRegisters(control.rhID, addrInit, nRegs, regs);
    } else {
    	#if (LOG_MODBUS == pdON)
    	//fprintf(flog, "modbus ReadRegs [cmd %d] [slave %d] [reg 0x%x] [len %d]"CMD_TERMINATOR, cmd, control.rhID, addrInit, nRegs);
    	printf("modbus ReadRegs [cmd %d] [slave %d] [reg 0x%x] [len %d]"CMD_TERMINATOR, cmd, control.rhID, addrInit, nRegs);
    	#endif
		ret = modbus_MasterReadRegisters(control.rhID, addrInit, nRegs, regs);
	}

	// se foi enviado com sucesso ficaremos na espera da resposta do recurso de hardware
	if (ret == pdPASS) waitResponse = pdTRUE;
	#if (LOG_MODBUS == pdON)
	//else fprintf(flog, "modbus err[%d] send querie"CMD_TERMINATOR, modbus_MasterReadStatus());
	else printf("modbus err[%d] SEND querie"CMD_TERMINATOR, modbus_MasterReadStatus());
	#endif

		return;
}



void init_control_tad(){
    int x; 
	control.funcEx = funcPANEL_ELETRIC;		// Sinaliza que este MSIP vai gerenciar o experimento do painel elétrico
	control.rhID = 1;						// Sinaliza que o endereço do RH no modbus é 1
	control.getInfo = 0; 					// sinaliza para pegar as informações do RH
	control.getRelays = 0;
	control.getDouts = 0;
	control.exit = 0;
	control.douts = 1;
	control.relays = 1;
	control.relaysOld = control.doutsOld = 1;
	control.nMultimetersGeren = nMULTIMETER_GEREN;
	control.stsCom = 0;
	memset(control.rhModel, '\0', __STRINGSIZE__);
	memset(control.rhFirmware, '\0', __STRINGSIZE__);
	for(x=0;x<nMULTIMETER;x++) {
		control.multimeter[x].stsCom = 0;
		control.multimeter[x].sts = 0;
	}
  return;
	
}

// processo do modbus.
//	Neste processo gerencia os envios de comandos para o recurso de hardware e fica no aguardo de sua resposta
//	Atualiza as variaveis do sistema de acordo com a resposta do recurso de hardware.

void * modbus_Process(void * params) {
	
	int first = 0;
	while (control.exit == 0){
		if(first){
			usleep(0);
			
		}
	 first =1;

        modbus_MasterProcess();

		// Gerenciador de envio de comandos
		// se não estamos esperando a resposta do SendCommand vamos analisar o próximo comando a ser enviado
		if (!waitResponse) {
			// checa se é para pegar as informações do RH
			if (control.getInfo) {
				modbus_SendCommand(cmdGET_INFOS);

			// se o MSIP estiver configurado para gerenciar o painel elétrico
			} else if (control.funcEx == funcPANEL_ELETRIC) {
				// checa se houve mudanças nos estado dos reles
				if (control.relaysOld != control.relays) modbus_SendCommand(cmdSET_RELAYS);
				// checa se houve mudanças nos estados das saídas digitais
				else if (control.doutsOld != control.douts) modbus_SendCommand(cmdSET_DOUTS);
				// checa se houve um pedido de leitura de estados dos reles // TODO quando fazer atualizar control.relay também para que não envio o comando de ajuste
				// checa se houve um pedido de leitura dos estados saídas digitas// TODO quando fazer atualizar control.dout também para que não envio o comando de ajuste
				// Envia o pedido da leitura dos multimetros
				else modbus_SendCommand(cmdGET_MULTIMETERS);
			}
			continue;
		}


		int ret = modbus_MasterReadStatus();
		//	BUSY: Ficar na espera da resposta
		//  ERROR: Notificar  o erro e tomar procedimento cabíveis
		//  OK para escrita: Nada, pois os valores dos registradores foram salvos no escravo com sucesso
		//	OK para Leitura: Capturar os valores dos registradores lidos do escravo

		// se ainda está ocupado não faz nada

		if (ret == errMODBUS_BUSY) continue;
		waitResponse = pdFALSE;
		// se aconteceu algum erro
		if (ret < 0) {
			#if (LOG_MODBUS == pdON)
			//fprintf(flog, "modbus err[%d] wait response "CMD_TERMINATOR, ret);
			printf("modbus err[%d] WAIT response "CMD_TERMINATOR, ret);
			#endif
			control.stsCom = modbus_MasterReadException();
				// modbusILLEGAL_FUNCTION: O multimetro recebeu uma função que não foi implementada ou não foi habilitada.
				// modbusILLEGAL_DATA_ADDRESS: O multimetro precisou acessar um endereço inexistente.
				// modbusILLEGAL_DATA_VALUE: O valor contido no campo de dado não é permitido pelo multimetro. Isto indica uma falta de informações na estrutura do campo de dados.
				// modbusSLAVE_DEVICE_FAILURE: Um irrecuperável erro ocorreu enquanto o multimetro estava tentando executar a ação solicitada.
			continue;
		}

		control.stsCom = 5; // sinaliza que a conexão foi feita com sucesso

		// ATUALIZA VARS QUANDO A COMUNICAÇÃO FOI FEITA COM SUCESSO
		// -----------------------------------------------------------------------------------------------------------------

		// Comando para ler os registradores: modelo e versão firmware do RH
		if (cmd == cmdGET_INFOS) {
			#if (LOG_MODBUS == pdON)
			printf("model %c%c%c%c"CMD_TERMINATOR, (regs[0] & 0xff), (regs[0] >> 8), (regs[1] & 0xff), (regs[1] >> 8));
			printf("firware %c.%c"CMD_TERMINATOR, (regs[2] & 0xff), (regs[2] >> 8));
			#endif
			control.rhModel[0] = (regs[0] & 0xff);
			control.rhModel[1] = (regs[0] >> 8);
			control.rhModel[2] = (regs[1] & 0xff);
			control.rhModel[3] = (regs[1] >> 8);
			control.rhModel[4] = 0;
			control.rhFirmware[0] = (regs[2] & 0xff);
			control.rhFirmware[1] = (regs[2] >> 8);
			control.rhFirmware[2] = 0;

			control.getInfo = 0; // sinalizo para não pegar mais informações

		// comando para ajuste dos reles, vamos sinalizar para não enviar mais comandos
		} else if (cmd == cmdSET_RELAYS) {
			control.relaysOld = control.relays;
			#if (LOG_MODBUS == pdON)
			printf("RELAY set 0x%x"CMD_TERMINATOR, control.relaysOld);
			#endif
		// comando para ajuste dos reles, vamos sinalizar para não enviar mais comandos
		} else if (cmd == cmdSET_DOUTS) {
			control.doutsOld = control.douts;
			#if (LOG_MODBUS == pdON)
			printf("DOUTS set 0x%x"CMD_TERMINATOR, control.doutsOld);
			#endif

		// comando para ler os estados dos reles
		} else if (cmd == cmdGET_RELAYS) {
			control.relays = regs[0];
			control.relaysOld = regs[0];
			#if (LOG_MODBUS == pdON)
			printf("RELAY get 0x%x"CMD_TERMINATOR, control.relaysOld);
			#endif

		// comando para ler os estados das saidas digitais
		} else if (cmd == cmdGET_DOUTS) {
			control.douts = regs[0];
			control.doutsOld = regs[0];
			#if (LOG_MODBUS == pdON)
			printf("DOUTS get 0x%x"CMD_TERMINATOR, control.doutsOld);
			#endif
		// Comando para ler os multimetros
		} else if (cmd == cmdGET_MULTIMETERS) {
			uint x; for(x=0; x<control.nMultimetersGeren; x++) {
				control.multimeter[x].stsCom = regs[4*x] & 0xff;
				control.multimeter[x].func = (regs[(4*x)+1] & 0x10) >> 4;
				control.multimeter[x].sts = regs[(4*x)+1] & 0xf;
				control.multimeter[x].value = (regs[(4*x)+2]) | ( regs[(4*x)+3] << 16);
				#if (LOG_MODBUS == pdON)
				printf("MULTIMETER[%d] stsCom 0x%x func %d sts 0x%x value %d"CMD_TERMINATOR,
					x,
					control.multimeter[x].stsCom,
					control.multimeter[x].func,
					control.multimeter[x].sts,
					control.multimeter[x].value
				);
				#endif
			}
		}
    }
  
  return NULL;
}




NAN_METHOD(Setup) {
     NanScope();
     FILE *flog = fopen("msip.log", "w");

     if (modbus_Init() == pdFAIL) {
   		fprintf(flog, "Erro ao abrir a porta UART"CMD_TERMINATOR);
		fprintf(flog, "Verifique se essa porta não esteja sendo usada por outro programa,"CMD_TERMINATOR);
		fprintf(flog, "ou se o usuário tem permissão para usar essa porta: chmod a+rw %s"CMD_TERMINATOR, COM_PORT);
		NanReturnValue(NanNew("0"));

     }
      
    fclose(flog);

     NanReturnValue(NanNew("1"));

}

NAN_METHOD(Update) {
	NanScope();
	char argv[40];
    std::string str_argv = *v8::String::Utf8Value(args[0]->ToString()); 
    strcpy(argv, str_argv.c_str());

    int switches = atoi(argv); 

	if(switches < 0 || switches > 256){
		 NanReturnValue(NanNew("{'error':'invalid input'}"));
	}
	control.relays= switches;
	printf("Old: %i Relays: %i"CMD_TERMINATOR,control.relaysOld, switches);
    NanReturnValue(NanNew("ok"));	

}

NAN_METHOD(GetValues) {
	NanScope();
	int i; 
	std::string buffer = "{";
	std::string buffer_amp = "", buffer_volt = "";
	char value[10];
	buffer_amp = std::string("\"amperemeter\":[");
	buffer_volt = std::string("\"voltmeter\":[");

	for(i =0; i < nMULTIMETER_GEREN ; i++){
		if(control.multimeter[i].sts)
				sprintf( value, "%i", control.multimeter[i].value);
		else	
				sprintf( value, "%i", 0);
			
		
		if(control.multimeter[i].func){
			if(i == 0)
				buffer_amp = buffer_amp + std::string(value) ;
			else
				buffer_amp = buffer_amp +  std::string(",") + std::string(value) ;
			
			
		}else{
			if(i == nMULTIMETER_GEREN-1)
				buffer_volt = buffer_volt +  std::string(value) ;
			else
				buffer_volt = buffer_volt +  std::string(value) + std::string(",");
			
			
		}				
	}
	buffer_amp = buffer_amp + std::string("]");
    buffer_volt = buffer_volt + std::string("]");
	
	 ;
	
    NanReturnValue(NanNew("{" + buffer_amp + "," + buffer_volt + "}"));	

}


NAN_METHOD(Run) {
	NanScope();
        
    init_control_tad();
	
        int rthr = pthread_create(&id_thread, NULL, modbus_Process, (void *) 0); // fica funcionando até que receba um comando via WEB para sair		
		if(rthr){
			printf("Unable to create thread void * modbus_Process");

		}
		
	NanReturnValue(NanNew("1"));

}


NAN_METHOD(Exit) {
	NanScope();
        FILE * flog = fopen("msip.log", "w");
        init_control_tad();

	char argv[40];
        std::string str_argv = *v8::String::Utf8Value(args[0]->ToString());
        
        strcpy(argv, str_argv.c_str());


	int switches = atoi(argv); 
	
	printf("Relays: %i"CMD_TERMINATOR,switches);

    if(switches < 0 || switches > 256){
		 NanReturnValue(NanNew("{'error':'invalid input'}"));
	}
	control.relays= switches;
	printf("Relays: %i"CMD_TERMINATOR,switches);
	usleep(50000);
	control.exit =1;
       
	printf("Fechando programa"CMD_TERMINATOR);
	fclose(flog);

	uart_Close();
	NanReturnValue(NanNew("1"));

}


void Init(Handle<Object> exports) {
	exports->Set(NanNew("run"), NanNew<FunctionTemplate>(Run)->GetFunction());
	exports->Set(NanNew("setup"), NanNew<FunctionTemplate>(Setup)->GetFunction());
	exports->Set(NanNew("update"), NanNew<FunctionTemplate>(Update)->GetFunction());
	exports->Set(NanNew("exit"), NanNew<FunctionTemplate>(Exit)->GetFunction());
	exports->Set(NanNew("getvalues"), NanNew<FunctionTemplate>(GetValues)->GetFunction());

}


NODE_MODULE(panel, Init)

