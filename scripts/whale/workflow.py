"""One host entry point; wait in this process and report one bounded result."""
import argparse
import json
from pathlib import Path
import time

from workflow_core import TaskRunner, command_for


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation',choices=['check','test','assets','build','package','device-sync','device-rollback','list','cancel'])
    parser.add_argument('--id')
    parser.add_argument('--timeout',type=int,default=600)
    args=parser.parse_args()
    if not 1<=args.timeout<=7200: parser.error('timeout必须在1..7200秒')
    root=Path(__file__).resolve().parents[2]
    runner=TaskRunner(root)
    try:
        if args.operation=='list': result=runner.list()
        elif args.operation=='cancel': result=dict(cancel_requested=runner.cancel(args.id or ''))
        else:
            job=runner.start(args.operation,command_for(root,args.operation),timeout=args.timeout)
            deadline=time.monotonic()+args.timeout+30
            while True:
                result=runner.get(job['id'])
                if result['status'] not in ('queued','running'): break
                if time.monotonic()>deadline:
                    runner.cancel(job['id'])
                    result=dict(status='failed',reason='执行器未及时收尾，结果待复核',firmware_written=False)
                    break
                time.sleep(.15)
    except (OSError,ValueError,RuntimeError) as error:
        result=dict(status='blocked',reason=str(error),firmware_written=False)
    print(json.dumps(result,ensure_ascii=False,indent=2))
    return 0 if isinstance(result,list) or result.get('status')=='succeeded' or args.operation=='cancel' else 2


if __name__=='__main__': raise SystemExit(main())
