#include "upgrade.h"
#include <string.h>
#include "crc.h"

struct upgrade_data_t upgrade_data;

static HAL_StatusTypeDef flash_erase(uint32_t addr, uint32_t size)
{
  uint32_t PageError;
  FLASH_EraseInitTypeDef def;
  HAL_StatusTypeDef status = HAL_ERROR;
  
  HAL_FLASH_Unlock();
  
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGERR);
  
  def.TypeErase = FLASH_TYPEERASE_PAGES;
  def.PageAddress = addr;
  def.NbPages = size / FLASH_PAGE_SIZE;
  if (def.NbPages * FLASH_PAGE_SIZE < size)
  {
    def.NbPages += 1;
  }
  def.Banks = FLASH_BANK_1;
  status = HAL_FLASHEx_Erase(&def, &PageError);
  
  HAL_FLASH_Lock();
  
  return status;
}

HAL_StatusTypeDef flash_stats;
static void flash_write(uint32_t addr, uint16_t * data, uint32_t data_len)
{
  uint16_t i;
  
  HAL_FLASH_Unlock();
  
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_OPTVERR | FLASH_FLAG_WRPERR | FLASH_FLAG_PGERR);

  for (i = 0; i < data_len / 2; i++)
  {
    flash_stats = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i * 2, data[i]);
  }
  
  HAL_FLASH_Lock();
}

static void set_upgrade_status(uint8_t upgrade_status)
{
  FLASH_OBProgramInitTypeDef OBInit;
  
  HAL_FLASHEx_OBGetConfig(&OBInit);
  OBInit.OptionType |= OPTIONBYTE_DATA;
  OBInit.DATAAddress = OB_DATA_ADDRESS_DATA0;
  OBInit.DATAData = upgrade_status;
  
  HAL_FLASH_Unlock();
  HAL_FLASH_OB_Unlock();
  
  HAL_FLASHEx_OBErase();
  HAL_FLASHEx_OBProgram(&OBInit);
  
  HAL_FLASH_OB_Lock();
  HAL_FLASH_Lock();
  
  __set_FAULTMASK(1);
  HAL_FLASH_OB_Launch();
}

uint8_t check_upgrade(void)
{
  uint32_t crc32;
  uint32_t *app_backup_end_addr = (uint32_t *)APP_PARA_ADDR;
  uint32_t *app_backup_start_addr = (uint32_t *)APP_BACKUP_ADDR;
  uint8_t upgrade_status;
  
  upgrade_status = HAL_FLASHEx_OBGetUserData(OB_DATA_ADDRESS_DATA0);
  if (upgrade_status != UPGRADE_INIT)
  {
    return 0;
  }
  
  do{
    app_backup_end_addr--;
    if (*app_backup_end_addr == UPGRADE_VALID_FLAG)
    {
      break;
    }
  }while (app_backup_end_addr > app_backup_start_addr);
  
  if (app_backup_end_addr <= app_backup_start_addr)
    return 0;
  
  memcpy(&upgrade_data, app_backup_end_addr, sizeof(struct upgrade_data_t));
  
  if (upgrade_data.upgrade_flag == UPGRADE_VALID_FLAG)
  {
      if (upgrade_data.upgrade_fileLength <= APP_SIZE)
      {
        crc32 = HAL_CRC_Calculate(&hcrc, app_backup_start_addr, upgrade_data.upgrade_fileLength / 4);
        if (crc32 == upgrade_data.upgrade_crc32)
        {
          return 1;
        }
        else
        {
          upgrade_status = UPGRADE_CHECK_SUM_ERROR;
        }
      }
      else
      {
        upgrade_status = UPGRADE_LENGTH_OUT_OF_RANGE;
      }
      
      set_upgrade_status(upgrade_status);
  }
  
  return 0;
}

static void jump2app(void)
{
  uint32_t app_check_address;
  __IO uint32_t *app_check_address_ptr;
  
  app_check_address = APP_ADDR;
  app_check_address_ptr = (__IO uint32_t *)app_check_address;
  
  /* Rebase the Stack Pointer */
  __set_MSP(*app_check_address_ptr);

  /* Rebase the vector table base address */
  SCB->VTOR = (app_check_address & SCB_VTOR_TBLOFF_Msk);
  
  /* Pointer to the Application Section */
  void (*application_code_entry)(void);

  /* Load the Reset Handler address of the application */
  application_code_entry = (void (*)(void))(*(app_check_address_ptr + 1));

  /* Jump to user Reset Handler in the application */
  application_code_entry();
  //asm("bx %0"::"r"(app_start_address));
}

uint32_t crc32;
uint8_t buf[FLASH_PAGE_SIZE];
void upgrade(void)
{
  uint32_t i;
  uint32_t *app_start_addr = (uint32_t *)APP_ADDR;
  
  if (!check_upgrade())
  {
    jump2app();
  }
  
  __set_FAULTMASK(1);
  flash_erase(APP_ADDR, APP_SIZE);
  
  for (i = 0; i < upgrade_data.upgrade_fileLength; i += FLASH_PAGE_SIZE)
  {
    memcpy(buf, (void*)(APP_BACKUP_ADDR + i), FLASH_PAGE_SIZE);
    if (memcmp(buf, (void*)(APP_BACKUP_ADDR + i), FLASH_PAGE_SIZE))
    {
      while(1);
    }
    flash_write(APP_ADDR + i, (uint16_t *)buf, FLASH_PAGE_SIZE);
    if (memcmp(buf, (void*)(APP_ADDR + i), FLASH_PAGE_SIZE))
    {
      while(1);
    }
  }
  
  crc32 = HAL_CRC_Calculate(&hcrc, app_start_addr, upgrade_data.upgrade_fileLength / 4);
  if (crc32 == upgrade_data.upgrade_crc32)
  {
    set_upgrade_status(UPGRADE_SUCCESS);
  }
  else
  {
    while(1);
    //HAL_FLASH_OB_Launch();
  }
}
