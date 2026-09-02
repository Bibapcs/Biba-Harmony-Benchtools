import { DriverExtensionAbility } from '@kit.DriverDevelopmentKit';
import { rpc } from '@kit.IPCKit';
import { Want } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import testNapi from 'libentry.so';

const TAG: string = 'bbpcs-i1driver';

// RPC 命令码
export const CODE_PROBE: number = 1;
export const CODE_INIT: number = 2;
export const CODE_CALIBRATE: number = 3;
export const CODE_READ_PATCH: number = 4;
export const CODE_CLOSE: number = 5;

class I1DriverStub extends rpc.RemoteObject {
  onRemoteMessageRequest(code: number, data: rpc.MessageSequence, reply: rpc.MessageSequence,
    options: rpc.MessageOption): boolean {
    hilog.info(0, TAG, `onRemoteMessageRequest code=${code}`);
    try {
      if (code === CODE_PROBE) {
        reply.writeString(testNapi.i1Probe());
        return true;
      }
      if (code === CODE_INIT) {
        reply.writeString(testNapi.i1mInit());
        return true;
      }
      if (code === CODE_CALIBRATE) {
        reply.writeString(testNapi.i1mCalibrate());
        return true;
      }
      if (code === CODE_READ_PATCH) {
        reply.writeString(testNapi.i1mReadPatch());
        return true;
      }
      if (code === CODE_CLOSE) {
        testNapi.i1mClose();
        reply.writeString('OK');
        return true;
      }
    } catch (e) {
      reply.writeString(`ERR driver exception: ${JSON.stringify(e)}`);
      return true;
    }
    return false;
  }
}

export default class I1DriverAbility extends DriverExtensionAbility {
  onInit(want: Want): void {
    const params = want.parameters as Record<string, Object>;
    if (params !== null && params !== undefined) {
      const deviceId = params['deviceId'] as number;
      hilog.info(0, TAG, `onInit deviceId=${deviceId}`);
      testNapi.i1SetDeviceId(deviceId);
    } else {
      hilog.error(0, TAG, 'onInit: no parameters');
    }
  }

  onConnect(want: Want): rpc.RemoteObject {
    hilog.info(0, TAG, 'onConnect');
    return new I1DriverStub('i1driver');
  }

  onDisconnect(want: Want): void {
    hilog.info(0, TAG, 'onDisconnect');
    try {
      testNapi.i1mClose();
    } catch (e) {
      hilog.error(0, TAG, 'i1mClose exception');
    }
  }

  onRelease(): void {
    hilog.info(0, TAG, 'onRelease');
  }
}
