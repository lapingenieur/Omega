#include "device.h"
#include "../regs/regs.h"

namespace Ion {
namespace USB {
namespace Device {

void Device::init() {
  // Wait for AHB idle
  while (!OTG.GRSTCTL()->getAHBIDL()) {
  }

  // Core soft reset
  OTG.GRSTCTL()->setCSRST(true);
  while (OTG.GRSTCTL()->getCSRST()) {
  }

  // Enable the USB transceiver
  OTG.GCCFG()->setPWRDWN(true);
  // FIXME: Understand why VBDEN is required
  OTG.GCCFG()->setVBDEN(true);

  // Get out of soft-disconnected state
  OTG.DCTL()->setSDIS(false);

  // Force peripheral only mode
  OTG.GUSBCFG()->setFDMOD(true);

  /* Configure the USB turnaround time.
   * This has to be configured depending on the AHB clock speed. */
  OTG.GUSBCFG()->setTRDT(0x6);

  // Clear the interrupts
  OTG.GINTSTS()->set(0);

  // Full speed device
  OTG.DCFG()->setDSPD(OTG::DCFG::DSPD::FullSpeed);

  // FIFO-size = 128 * 32bits
  // FIXME: Explain :-) Maybe we can increase it.
  OTG.GRXFSIZ()->setRXFD(128);

  // Unmask the interrupt line assertions
  OTG.GAHBCFG()->setGINTMSK(true);

  // Restart the PHY clock.
  OTG.PCGCCTL()->setSTPPCLK(0);

  // Pick which interrupts we're interested in
  class OTG::GINTMSK intMask(0); // Reset value
  intMask.setENUMDNEM(true); // Speed enumeration done
  intMask.setUSBRST(true); // USB reset
  intMask.setRXFLVLM(true); // Receive FIFO non empty
  intMask.setIEPINT(true); // IN endpoint interrupt
  intMask.setWUIM(true); // Resume / wakeup
  intMask.setUSBSUSPM(true); // USB suspend
  OTG.GINTMSK()->set(intMask);

  // Unmask IN endpoint interrupt 0
  OTG.DAINTMSK()->setIEPM(1);

  // Unmask the transfer completed interrupt
  OTG.DIEPMSK()->setXFRCM(true);

  // Wait for an USB reset
  while (!OTG.GINTSTS()->getUSBRST()) {
  }

  // Wait for ENUMDNE
  while (!OTG.GINTSTS()->getENUMDNE()) {
  }

  while (true) {
    poll();
  }
}

void Device::shutdown() {
  //TODO ?
}

void Device::poll() {

  // Read the interrupts
  class OTG::GINTSTS intsts(OTG.GINTSTS()->get());

  /* SETUP or OUT transaction
   * If the Rx FIFO is not empty, there is a SETUP or OUT transaction.
   * The interrupt is done AFTER THE HANSDHAKE of the transaction. */

  if (intsts.getRXFLVL()) {
    class OTG::GRXSTSP grxstsp(OTG.GRXSTSP()->get());

    // Store the packet status
    OTG::GRXSTSP::PKTSTS pktsts = grxstsp.getPKTSTS();

    // We only use endpoint 0
    assert(grxstsp.getEPNUM() == 0); // TODO assert or return?

    if (pktsts == OTG::GRXSTSP::PKTSTS::OutTransferCompleted || pktsts == OTG::GRXSTSP::PKTSTS::SetupTransactionCompleted) {
      // Reset the out endpoint
      m_ep0.setupOut();
      // Set the NAK bit
      m_ep0.setOutNAK(m_ep0.NAKForced());
      // Enable the endpoint
      m_ep0.enableOut();
      return;
    }

    if (pktsts != OTG::GRXSTSP::PKTSTS::OutReceived && pktsts != OTG::GRXSTSP::PKTSTS::SetupReceived) {
      return; // TODO other option: global Out Nak. what to do?
    }

    TransactionType type = (pktsts == OTG::GRXSTSP::PKTSTS::OutReceived) ? TransactionType::Out : TransactionType::Setup;

    if (type == TransactionType::Setup && OTG.DIEPTSIZ0()->getPKTCNT()) {
      // SETUP received but there is something in the Tx FIFO. Flush it.
      m_ep0.flushTxFifo();
    }

    // Save the received packet byte count
    m_ep0.setReceivedPacketSize(grxstsp.getBCNT());

    if (type == TransactionType::Setup) {
      m_ep0.processSETUPpacket();
    } else {
      assert(type == TransactionType::Out);
      m_ep0.processOUTpacket();
    }

    m_ep0.discardUnreadData();
  }

  /* IN transactions
   * The interrupt is done AFTER THE HANSDHAKE of the transaction. */
  if (OTG.DIEPINT(0)->getXFRC()) {
    m_ep0.processINpacket();
    // Clear the Transfer Completed Interrupt
    OTG.DIEPINT(0)->setXFRC(true);
  }

  // Handle USB RESET. ENUMDNE = **SPEED** Enumeration Done
  if (intsts.getENUMDNE()) {
    // Clear the ENUMDNE bit
    OTG.GINTSTS()->setENUMDNE(true);
    setAddress(0);
    // Flush the FIFOs
    m_ep0.reset();
    m_ep0.setup();
    return;
  }

  // Handle Suspend interrupt: clear it
  if (intsts.getUSBSUSP()) {
    OTG.GINTSTS()->setUSBSUSP(true);
  }

  // Handle WakeUp interrupt: clear it
  if (intsts.getWKUPINT()) {
    OTG.GINTSTS()->setWKUPINT(true);
  }

  // Handle StartOfFrame interrupt: clear it
  if (intsts.getSOF()) {
    OTG.GINTSTS()->setSOF(true);
  }
}

/*void Device::processSetupRequest(SetupPacket * request, uint8_t * transferBuffer, uint16_t * transferBufferLength, uint16_t transferBufferMaxLength) {
  if (request->nextTransactionIsOUT()) {
    // The folllowing transaction will be an OUT transaction.
    *transferBufferLength = 0;
    // Set the Device state.
    if (request->wLength() > Endpoint0::k_maxPacketSize) {
      m_state = State::DataOut;
    } else {
      m_state = State::LastDataOut;
    }
    m_ep0.setOutNAK(false);
    return;
  }

  // The folllowing transaction will be an IN transaction.
  *transferBufferLength = request->wLength();

  if (processInSetupRequest(request, transferBuffer, transferBufferLength, transferBufferMaxLength)) {
    if (request.wLength() > 0) {
      // The host is waiting for device data. Check if we need to send a Zero
      // Length Packet to explicit a short transaction.
      m_ep0->computeZeroLengthPacketNeeded();
      // Send the data.
      m_ep0->sendSomeData();
    } else {
      // If no data is expected, send a zero length packet
      *transferBufferLength = 0;
      m_ep0->sendSomeData();
      m_state = State::StatusIn;
    }
    return;
  }
  // Stall endpoint on failure
  m_ep0.stallTransaction();
}

bool Device::processInSetupRequest(SetupPacket request, uint8_t * transferBuffer, uint16_t * transferBufferLength, uint16_t * transferBufferMaxLength) {
  switch (request.bRequest()) {
    case k_requestGetStatus:
      return getStatus(transferBuffer, transferBufferLength);
      break;
    case k_requestSetAddress:
      if ((request.bmRequestType() != 0) || (request.wValue() >= 128)) {
        return false;
      }
      /* According to the reference manual, the address should be set after the
       * Status stage of the current transaction, but this is not true.
       * It should be set here, after the Data stage. */
 /*     setAddress(request.wValue());
      return true;
      break;
    case k_requestGetDescriptor:
      return getDescriptor(request, transferBuffer, transferBufferLength, transferBufferMaxLength);
      break;
    case k_requestSetConfiguration:
      return setConfiguration(request);
    default:
      //TODO other cases not needed?
      break;
  }
  return false;
}*/

bool Device::getStatus(uint8_t * transferBuffer, uint16_t * transferBufferLength) {
  if (*transferBufferLength > 2) {
    *transferBufferLength = 2;
  }
  //TODO check, in bmRequestType, who is the recipient: Device, interface or endpoint? And fill in the status correclty. See http://www.usbmadesimple.co.uk/ums_4.htm
  transferBuffer[0] = 0;
  transferBuffer[1] = 0;
  //TODO Dirty
  return true;
}

void Device::setAddress(uint8_t address) {
  OTG.DCFG()->setDAD(address);
}

bool Device::getDescriptor(SetupPacket * request, uint8_t * transferBuffer, uint16_t * transferBufferLength, uint16_t transferBufferMaxLength) {
  Descriptor * wantedDescriptor = descriptor(request->descriptorType(), request->descriptorIndex());
  if (wantedDescriptor == nullptr) {
    return false;
  }
  *transferBufferLength = wantedDescriptor->copy(transferBuffer, transferBufferMaxLength);
  return true;
}

bool Device::setConfiguration(SetupPacket * request) {
  // We support one configuration only
  if (request->wValue() != 0) { //TODO this is bConfigurationValue in the configuration descriptor
    return false;
  }
  /* There is one configuration only, we no need to set it again, just reset the
   * endpoint. */
  m_ep0.reset();
  return true;
}

}
}
}
