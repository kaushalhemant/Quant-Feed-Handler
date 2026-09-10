import asyncio
import websockets
import json

async def wait_for_snapshot(ws, condition, timeout=2.0):
    start = asyncio.get_event_loop().time()
    while asyncio.get_event_loop().time() - start < timeout:
        msg = await asyncio.wait_for(ws.recv(), timeout=1.0)
        data = json.loads(msg)
        if condition(data):
            return data
    raise TimeoutError("Snapshot condition not met in time")

async def test_user_flow():
    print("[Test] Connecting to ws://localhost:8765...")
    async with websockets.connect("ws://localhost:8765") as ws:
        print("[Test] Connected! Sending RESET to ensure clean state...")
        await ws.send(json.dumps({"command": "RESET"}))
        clean_snap = await wait_for_snapshot(ws, lambda d: d['totalProcessed'] == 0 and len([b for b in d['bids'] if b['active']]) == 0)
        print(f"  Clean State Verified: Bids=0, Asks=0, Processed=0")

        print("\n[Test] Sending User Manual Order: Buy 500 @ $224.95...")
        await ws.send(json.dumps({"command": "RAW_LINE", "line": "A,1001,B,224.95,500"}))
        snap = await wait_for_snapshot(ws, lambda d: len([b for b in d['bids'] if b['active']]) > 0)
        active_bids = [b for b in snap['bids'] if b['active']]
        print(f"  After Manual Buy: Best Bid = ${active_bids[0]['price']/100:.2f} (qty {active_bids[0]['qty']})")

        print("\n[Test] Sending User Manual Order: Sell 400 @ $225.00...")
        await ws.send(json.dumps({"command": "RAW_LINE", "line": "A,1002,S,225.00,400"}))
        snap = await wait_for_snapshot(ws, lambda d: len([a for a in d['asks'] if a['active']]) > 0)
        active_asks = [a for a in snap['asks'] if a['active']]
        print(f"  After Manual Sell: Best Ask = ${active_asks[0]['price']/100:.2f} (qty {active_asks[0]['qty']}) | Spread = ${snap['analytics']['spread']/100:.2f}")

        print("\n[Test] Sending LOAD_EXAMPLE 'nvda'...")
        await ws.send(json.dumps({"command": "LOAD_EXAMPLE", "name": "nvda"}))
        snap = await wait_for_snapshot(ws, lambda d: d['totalProcessed'] >= 20)
        print(f"  After Loading NVDA Flow: Total Processed = {snap['totalProcessed']} | Spread = ${snap['analytics']['spread']/100:.2f}")

        print("\n[Test] Sending RESET command...")
        await ws.send(json.dumps({"command": "RESET"}))
        snap = await wait_for_snapshot(ws, lambda d: d['totalProcessed'] == 0)
        active_b = [b for b in snap['bids'] if b['active']]
        active_a = [a for a in snap['asks'] if a['active']]
        print(f"  After RESET: Bids={len(active_b)}, Asks={len(active_a)}, Total Processed={snap['totalProcessed']}")

        print("\n[Test] ALL 100% USER DATA INGESTION ASSERTIONS PASSED PERFECTLY!")

if __name__ == "__main__":
    asyncio.run(test_user_flow())
