/**
  ******************************************************************************
  * @file    usbd_cli.c
  * @author  Katagiri
  * @brief   Source file for USB command line's commands.
  ******************************************************************************
  */
/* Includes ------------------------------------------------------------------*/
#include "usbd_cli.h"
#include "usbd_cli_commands.h"

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
int8_t TEST(uint8_t* pArg, uint8_t* pRes);

/* Exported variables --------------------------------------------------------*/

/********************* Command definition *********************** 
  - Name and the function have to be filled in CW_CI_CommandSet for each command.
  - Type of function is CommandFxn,
      int8_t (*CommandFxn)(uint8_t* pArg, uint8_t* pRes)
  - String after the space following command name is passed to the function as arguments.
  - Each command functions have to parse the arguments individually.
*/
// Set of command function
CommandUnit CommandSet[] =
{
  {"GET_LOG", TEST},
};

/****************************************************************/ 

// Number of commands
const uint16_t NumOfCommands = sizeof(CommandSet)/sizeof(CommandUnit);

/* Private functions ---------------------------------------------------------*/
int8_t TEST(uint8_t* pArg, uint8_t* pRes)
{
  uint8_t i;

  if(pArg != NULL)
  {
    return CLI_RESULT_INVALID;
  }
  else
  {
    for(i=0; i<26; i++)
    {
      pRes[i] = i + (uint8_t)'a';
    }
  }

  return CLI_RESULT_OK;
}