#include "RF95Interface.h"
#include "MeshRadio.h" // kinda yucky, but we need to know which region we are in

#include <configuration.h>

RF95Interface::RF95Interface(RADIOLIB_PIN_TYPE cs, RADIOLIB_PIN_TYPE irq, RADIOLIB_PIN_TYPE rst, SPIClass &spi)
    : RadioLibInterface(cs, irq, rst, 0, spi)
{
    // FIXME - we assume devices never get destroyed
}

/// Initialise the Driver transport hardware and software.
/// Make sure the Driver is properly configured before calling init().
/// \return true if initialisation succeeded.
bool RF95Interface::init()
{
    applyModemConfig();
    if (power > 20) // This chip has lower power limits than some
        power = 20;

    int res;
    /**
     * We do a nasty check on freq range to figure our RFM96 vs RFM95
     *
     */
    if (CH0 < 530.0) {
        auto dev = new RFM96(&module);
        iface = lora = dev;
        res = dev->begin(freq, bw, sf, cr, syncWord, power, currentLimit, preambleLength);
    } else {
        auto dev = new RFM95(&module);
        iface = lora = dev;
        res = dev->begin(freq, bw, sf, cr, syncWord, power, currentLimit, preambleLength);
    }
    DEBUG_MSG("LORA init result %d\n", res);

    if (res == ERR_NONE)
        res = lora->setCRC(SX126X_LORA_CRC_ON);

    if (res == ERR_NONE)
        startReceive(); // start receiving

    return res == ERR_NONE;
}

void INTERRUPT_ATTR RF95Interface::disableInterrupt()
{
    lora->clearDio0Action();
}

bool RF95Interface::reconfigure()
{
    applyModemConfig();

    // set mode to standby
    setStandby();

    // configure publicly accessible settings
    int err = lora->setSpreadingFactor(sf);
    assert(err == ERR_NONE);

    err = lora->setBandwidth(bw);
    assert(err == ERR_NONE);

    err = lora->setCodingRate(cr);
    assert(err == ERR_NONE);

    err = lora->setSyncWord(syncWord);
    assert(err == ERR_NONE);

    err = lora->setCurrentLimit(currentLimit);
    assert(err == ERR_NONE);

    err = lora->setPreambleLength(preambleLength);
    assert(err == ERR_NONE);

    err = lora->setFrequency(freq);
    assert(err == ERR_NONE);

    if (power > 20) // This chip has lower power limits than some
        power = 20;
    err = lora->setOutputPower(power);
    assert(err == ERR_NONE);

    startReceive(); // restart receiving

    return ERR_NONE;
}

void RF95Interface::setStandby()
{
    int err = lora->standby();
    assert(err == ERR_NONE);

    isReceiving = false; // If we were receiving, not any more
    completeSending();   // If we were sending, not anymore
    disableInterrupt();
}

void RF95Interface::startReceive()
{
    setStandby();
    int err = lora->startReceive();
    assert(err == ERR_NONE);

    isReceiving = true;

    // Must be done AFTER, starting transmit, because startTransmit clears (possibly stale) interrupt pending register bits
    enableInterrupt(isrRxLevel0);
}

/** Could we send right now (i.e. either not actively receving or transmitting)? */
bool RF95Interface::canSendImmediately()
{
    // We wait _if_ we are partially though receiving a packet (rather than just merely waiting for one).
    // To do otherwise would be doubly bad because not only would we drop the packet that was on the way in,
    // we almost certainly guarantee no one outside will like the packet we are sending.
    bool busyTx = sendingPacket != NULL;
    bool busyRx = false; // FIXME - use old impl.  isReceiving && lora->getPacketLength() > 0;

    if (busyTx || busyRx)
        DEBUG_MSG("Can not set now, busyTx=%d, busyRx=%d\n", busyTx, busyRx);

    return !busyTx && !busyRx;
}

bool RF95Interface::sleep()
{
    // put chipset into sleep mode
    disableInterrupt();
    lora->sleep();

    return true;
}