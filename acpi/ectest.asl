//
// EC Test interface to load KMDF driver and map methods
//
Device (ECT0) {
  Name (_HID, "ACPI1234")
  Name (_UID, 0x0)
  Name (_CCA, 0x0)

  Name (NEVT, 0x1234) 

  Method (_STA) {
    Return (0xf)
  }

  Method (TFST,0,Serialized) {
    Return ( \_SB.FAN0._FST() )
  }

  Method(TEST, 0x0, NotSerialized) {  
    TNFY()
    Return( TFWS() )
  }

  // EC_SVC_MANAGEMENT 330c1273-fde5-4757-9819-5b6539037502
  Method(TFWS, 0x0, Serialized) {  
    If(LEqual(\_SB.FFA0.AVAL,One)) {
      Name(BUFF, Buffer(128){})
    
      CreateByteField(BUFF,0,STAT) // Out – Status for req/rsp 
      CreateByteField(BUFF,1,LENG) // In/Out – Bytes in req, updates bytes returned 
      CreateField(BUFF,16,128,UUID) // UUID of service 
      CreateByteField(BUFF,18,CMDD) // Command register
      
      CreateField(BUFF,208,32,FWSD) // Output Data
      // x0 -> STAT

      Store(0x20, LENG)
      Store(0x1, CMDD) // EC_CAP_GET_FW_STATE
      Store(ToUUID("330c1273-fde5-4757-9819-5b6539037502"), UUID)
      Store(Store(BUFF, \_SB_.FFA0.FFAC), BUFF)
      If(LEqual(STAT,0x0) ) // Check FF-A successful?
      {
        Return (FWSD)
      } else {
        Return(Zero)
      }
    } else {
      Return(Zero)
    }
  }

  // Call test API to send notification event
  Method(TNFY, 0x0, Serialized) {  
    If(LEqual(\_SB.FFA0.AVAL,One)) {
      Name(BUFF, Buffer(128){})
    
      CreateByteField(BUFF,1,LENG) // In/Out – Bytes in req, updates bytes returned 
      CreateField(BUFF,16,128,UUID) // UUID of service 
      CreateByteField(BUFF,18,CMDD) // Command register
  
      Store(20, LENG)
      Store(4, CMDD) // EC_CAP_TEST_NFY
      Store(ToUUID("330c1273-fde5-4757-9819-5b6539037502"), UUID)
      Store(Store(BUFF, \_SB_.FFA0.FFAC), BUFF)
    }
  }

} // Device (ECT0)


Device(\_SB_.FFA0) {
  Name(_HID, "MSFT000C")
  
  // Register notification events from FFA
  Method(_RNY, 0, Serialized) {
    Return( Package() {
      Package(0x2) {
        ToUUID("330c1273-fde5-4757-9819-5b6539037502"),
        Buffer() {0x1,0x0,0x2,0x0,0x3,0x0} // Register events 0x1, 0x2, 0x3
      }
    } )
  }   

  OperationRegion(AFFH, FFixedHw, 4, 144) 
  Field(AFFH, BufferAcc, NoLock, Preserve) { AccessAs(BufferAcc, 0x1), FFAC, 1152 }     

  Method(_NFY, 2, Serialized) {
    // Arg0 == UUID
    // Arg1 == Notify ID

    If(LEqual(ToUUID("330c1273-fde5-4757-9819-5b6539037502"),Arg0)) {
      Store(Arg1, \_SB.ECT0.NEVT)
      Notify(\_SB.ECT0, 0x20)
    }

  }

  // Other components check this to make sure FFA is available
  Method(AVAL, 0, Serialized) {
    Return(One)
  }

}

