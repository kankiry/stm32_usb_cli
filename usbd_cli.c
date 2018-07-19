/**
  ******************************************************************************
  * @file    usbd_cli.c
  * @author  Katagiri
  * @brief   Source file for USB command line interpreter
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "usbd_def.h"
#include "usbd_cli.h"
#include "usbd_cli_commands.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
// message strings
#define STRING_CMD_OVERFLOW       "Error : Command buffer overflow."
#define STRING_OTHER              "Error : Unexpected problem occured."
#define STRING_CMD_NOTFOUND       "Error : Command not found."
#define STRING_ARG_INVALID        "Error : Argument invalid."

// error number
#define ERRNO_CMD_NOTFOUND        0
#define ERRNO_ARG_INVALID         1

// flags of CLI_Status
#define CLI_STATUS_ECHO            0x1
#define CLI_STATUS_NEWLINE         0x2
#define CLI_STATUS_PROMPT          0x4
#define CLI_STATUS_RESPONSE        0x8
#define CLI_STATUS_BREAK           0x10
#define CLI_STATUS_BUSY            0x20
#define CLI_STATUS_CMDOVF          0x100
#define CLI_STATUS_ERROR_MASK      0xF00

/* Private macro -------------------------------------------------------------*/
#define IS_STATUS(__FLAG__)                     ((CLI_Status & (__FLAG__)) == (__FLAG__))
#define IS_ANY_STATUS(__FLAG__)                 ((CLI_Status & (__FLAG__)) != 0)
#define SET_STATUS(__FLAG__)                    (CLI_Status |= (__FLAG__))
#define CLEAR_STATUS(__FLAG__)                  (CLI_Status &= ~(__FLAG__)) 
#define UPDATE_STATUS(__SFLAG__, __CFLAG__)     \
  do{                                           \
    uint16_t tmp = CLI_Status & ~(__CFLAG__);   \
    CLI_Status = tmp | (__SFLAG__);             \
  }while(0)
#define IS_CHAR_VALID(__CHAR__)                 (((__CHAR__ == '\r') || (__CHAR__ == '\n') || (' ' <= __CHAR__ && __CHAR__ <= '~')) ? 1 : 0)

/* Private function prototypes -----------------------------------------------*/
static void StrTrim(uint8_t **ppStr);
static void StrTrimR(uint8_t *pStr);
static void ResponseError(uint8_t* pRes, uint8_t ErrNo);
static int8_t ResponseError_CmdNotFound(uint8_t* pArg, uint8_t* pRes);
static int8_t BufferInput(uint8_t *pInput, uint16_t length);
static uint8_t* InvokeCommand(void);
static void ResetBuffer(void);

/* Exported function prototypes ----------------------------------------------*/
int8_t CLI_Input(uint8_t* pBuf, uint16_t dataLength);
uint8_t* CLI_Output(void);

/* Private variables ---------------------------------------------------------*/
static uint8_t String_Newline[] = CLI_STRING_NEWLINE;
static uint8_t String_Prompt[]  = CLI_STRING_PROMPT;
static uint8_t ErrorMessage_CmdOvf[] = STRING_CMD_OVERFLOW;
static uint8_t ErrorMessage_Other[]  = STRING_OTHER;
static uint8_t ErrorMessage_CmdNotFound[] = STRING_CMD_NOTFOUND;
static uint8_t ErrorMessage_ArgInvalid[] = STRING_ARG_INVALID;

static uint8_t CommandBuffer[CLI_COMMAND_LENGTH];   // store command string
static uint8_t ResponseBuffer[CLI_RESPONSE_LENGTH];   // store response of command
static uint16_t CmdBufIdxIn;                          // index of CommandBuffer to insert
static uint16_t CmdBufIdxOut;                         // index of CommandBuffer to echo
static uint16_t CLI_Status;                            // status of command line interpreter
static uint8_t* pResponse;                            // pointer of buffer to send to USB Host

extern CommandUnit CommandSet[];
extern const uint16_t NumOfCommands;

/* Exported functions --------------------------------------------------------*/
/**
  * @brief  CLI_Input: buffer input characters in command buffer and run command.
  * @param  pInput: pointer of input string
  * @param  length: length of input string
  * @retval Result
  */
int8_t CLI_Input(uint8_t* pInput, uint16_t length)
{
  // ignore input if busy
  if( IS_STATUS(CLI_STATUS_BUSY) || length == 0)
  {
    return (USBD_OK);
  }

  // copy input characters in command buffer.
  if( BufferInput(pInput, length) != CLI_RESULT_OK )
  {
    // buffer overflowed occured
    SET_STATUS(CLI_STATUS_BUSY | CLI_STATUS_CMDOVF);
    return USBD_OK;
  }

  // search newline code
  char *pNL = strstr((const char*)CommandBuffer, (const char*)String_Newline);
  if(pNL == NULL)
  {
    return (USBD_OK);
  }

  // terminate command string
  *pNL = '\0';

  // run command
  pResponse = InvokeCommand();
  SET_STATUS(CLI_STATUS_BUSY | CLI_STATUS_BREAK);  
  return (USBD_OK);
}

/**
  * @brief  CLI_Output: return some string
  * @retval Pointer of the buffer
  */
uint8_t* CLI_Output(void)
{
  uint8_t *pOutput = NULL;
  
  if( IS_ANY_STATUS(CLI_STATUS_ERROR_MASK))
  {
    uint16_t error_status;

    if( IS_STATUS(CLI_STATUS_CMDOVF) )
    {
      pResponse = ErrorMessage_CmdOvf;
      error_status = CLI_STATUS_CMDOVF;
    }

    UPDATE_STATUS(CLI_STATUS_RESPONSE | CLI_STATUS_NEWLINE, error_status | CLI_STATUS_ECHO); 
  }
  else if( IS_STATUS(CLI_STATUS_ECHO) )
  {
    if( CmdBufIdxOut < CmdBufIdxIn )
    {
      pOutput = &CommandBuffer[CmdBufIdxOut];
      CmdBufIdxOut += (uint16_t)strlen((const char*)&CommandBuffer[CmdBufIdxOut]);

      if( IS_STATUS(CLI_STATUS_BREAK) )
      {
        uint16_t next_status = ( 0 < (uint16_t)strlen((const char*)pOutput)) ? CLI_STATUS_RESPONSE : CLI_STATUS_PROMPT;
        UPDATE_STATUS(next_status | CLI_STATUS_NEWLINE, CLI_STATUS_ECHO | CLI_STATUS_BREAK);
      }
    }
  }
  else if( IS_STATUS(CLI_STATUS_NEWLINE) )
  {
    pOutput = String_Newline;
    CLEAR_STATUS(CLI_STATUS_NEWLINE);
  }
  else if( IS_STATUS(CLI_STATUS_RESPONSE) )
  {
    pOutput = pResponse;
    UPDATE_STATUS(CLI_STATUS_NEWLINE | CLI_STATUS_PROMPT, CLI_STATUS_RESPONSE);
  }
  else if( IS_STATUS(CLI_STATUS_PROMPT) )
  {
    pOutput = String_Prompt;
    ResetBuffer();
    UPDATE_STATUS(CLI_STATUS_ECHO, CLI_STATUS_PROMPT | CLI_STATUS_BUSY);
  }
  else  // unexpected error
  {
    pResponse = ErrorMessage_Other;
    SET_STATUS(CLI_STATUS_RESPONSE | CLI_STATUS_NEWLINE | CLI_STATUS_BUSY);
  }
  
  return pOutput;
}

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  StrTrim: Strip leading spaces
  * @param  ppStr: pointer to pointer of string
  * @retval None
  */
static void StrTrim(uint8_t **ppStr)
{
  while(*ppStr != '\0')
  {
    if(**ppStr == ' ')
    {
      ++(*ppStr);
    }
    else
    {
      return;
    }
  }
}

/**
  * @brief  StrTrimR: Strip trailing spaces
  * @param  pStr: pointer of string
  * @retval None
  */
static void StrTrimR(uint8_t* pStr)
{
  uint8_t* pEnd = (uint8_t*)((uint32_t)pStr + (uint32_t)strlen((const char*)pStr));
  while(pStr < pEnd)
  {
    --pEnd;
    if(*pEnd == ' ')
    {
      *pEnd = '\0';
    }
    else
    {
      return;
    }
  }
}

/**
  * @brief  ResponseError: Write error message in the buffer
  * @param  pRes: pointer of buffer
  * @param  ErrNo: error number
  * @retval None
  */
static void ResponseError(uint8_t* pRes, uint8_t ErrNo)
{
  uint16_t length;
  uint8_t *pMsg;

  if(ErrNo == ERRNO_CMD_NOTFOUND)
  {
    length = sizeof(ErrorMessage_CmdNotFound);
    pMsg = ErrorMessage_CmdNotFound;
  }
  else if(ErrNo == ERRNO_ARG_INVALID)
  {
    length = sizeof(ErrorMessage_ArgInvalid);
    pMsg = ErrorMessage_ArgInvalid;
  }
  else
  {
    return;
  }

  for(uint16_t i=0; i<length; i++)
  {
    pRes[i] = pMsg[i];
  }
}

/**
  * @brief  ResponseError_CmdNotFound: Write error message when command is not found.
  * @param  pArg: pointer of arguments string (just for command function interface)
  * @param  pRes: pointer of buffer
  * @param  ErrNo: error number
  * @retval None
  */
static int8_t ResponseError_CmdNotFound(uint8_t* pArg, uint8_t* pRes)
{
  ResponseError(pRes, ERRNO_CMD_NOTFOUND);
  return CLI_RESULT_OK;
}

/**
  * @brief  BufferInput: buffer input characters in command buffer and run command.
  * @param  pInput: pointer of input string
  * @param  length: length of input string
  * @retval Result
  */
static int8_t BufferInput(uint8_t *pInput, uint16_t length)
{
  int8_t result = CLI_RESULT_OK;
  for(uint32_t i=0; i<length; i++)
  {
    if( IS_CHAR_VALID(pInput[i]) )
    {
      CommandBuffer[CmdBufIdxIn++] = pInput[i];
      if( CLI_COMMAND_LENGTH <= CmdBufIdxIn)
      {
        result = CLI_RESULT_FAIL;
        break;
      }
    }
  }
  return result;
}

/**
  * @brief  InvokeCommand: search and run a command
  * @retval Pointer of output buffer
  */
static uint8_t* InvokeCommand(void)
{
  uint8_t *pCmd = CommandBuffer;
  uint8_t *pArg;
  CommandFxn Command = ResponseError_CmdNotFound;
  int8_t result;
  
  // strip extra spaces and get entry pointer of command
  StrTrim(&pCmd);
  StrTrimR(pCmd);

  // response empty if command is empty (all characters are ' '(SP))
  if((uint16_t)strlen((const char*)pCmd) == 0)
  {
    ResponseBuffer[0] = '\0';
    return ResponseBuffer;  
  }

  // get entry pointer of arguments
  pArg = (uint8_t*)strchr((const char*)pCmd, ' ');
  if(pArg != NULL)
  {
    *pArg = '\0';          // terminate command string
    ++pArg;                // entry of arguments string
    StrTrim(&pArg);        // strip extra leading spaces
  }

  // seek command
  for(uint16_t i=0; i<NumOfCommands; i++)
  {
    if(strcmp((const char*)pCmd, (const char*)CommandSet[i].name) == 0)
    {
      Command = CommandSet[i].command;
    }
  }
  
  // run command
  result = Command(pArg, ResponseBuffer);
  if(result == CLI_RESULT_INVALID)
  {
    ResponseError(ResponseBuffer, ERRNO_ARG_INVALID);
  }

  ResponseBuffer[CLI_RESPONSE_LENGTH - 1] = '\0';
  return ResponseBuffer;
}

/**
  * @brief  ResetBuffer: reset command buffer pointer and clear command & response buffer 
  * @retval None
  */
static void ResetBuffer(void)
{
  CmdBufIdxIn = 0;
  CmdBufIdxOut = 0;
  memset(CommandBuffer, 0, CLI_COMMAND_LENGTH);
  memset(ResponseBuffer, 0, CLI_RESPONSE_LENGTH);
}
