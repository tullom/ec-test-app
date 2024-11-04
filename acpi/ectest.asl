//
// EC Test interface to load KMDF driver and map methods
//
Device (ECT0) {
  Name (_HID, "ACPI1234")
  Name (_UID, 0x0)
  Name (_CCA, 0x0)

  Method (_STA) {
    Return (0xf)
  }

  Method (TFST,0,Serialized) {
    Return ( \_SB.FAN0._FST() )
  }

} // Device (ECT0)
