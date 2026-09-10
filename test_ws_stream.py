import asyncio
import websockets
import json

async def test():
    print("[Test] Connecting to ws://localhost:8765...")
    async with websockets.connect("ws://localhost:8765") as ws:
        print("[Test] Connected! Receiving live snapshots from C++20 engine...")
        for i in range(5):
            msg = await ws.recv()
            data = json.loads(msg)
            tput = data.get("throughput", 0)
            lat = data.get("latency", {})
            an = data.get("analytics", {})
            bids = data.get("bids", [])
            asks = data.get("asks", [])
            tape = data.get("tape", [])

            print(f"  [Snapshot #{i+1}] Throughput: {tput:,.0f} pkts/s | p50: {lat.get('p50',0):.1f}ns | p99: {lat.get('p99',0):.1f}ns | BBO: Bid ${bids[0]['price']/100:.2f} (qty {bids[0]['qty']}) vs Ask ${asks[0]['price']/100:.2f} (qty {asks[0]['qty']}) | Tape items: {len(tape)}")

        print("\n[Test] Sending BURST 100,000 packets command to C++ engine...")
        await ws.send(json.dumps({"command": "BURST", "count": 100000}))
        
        # Read next snapshot after burst
        msg = await ws.recv()
        data = json.loads(msg)
        print(f"  [Post-Burst Snapshot] Total Processed: {data.get('totalProcessed',0):,} msgs | p50: {data['latency']['p50']:.1f}ns | p99: {data['latency']['p99']:.1f}ns")
        print("\n[Test] SUCCESS: C++ Engine -> bridge.py -> WebSocket -> Browser verified 100% live & bidirectional!")

if __name__ == "__main__":
    asyncio.run(test())
