import pytest
import os
import sys

# Ensure repository root is on sys.path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from bridge import TokenBucketRateLimiter, format_order_command, sanitize_and_route_command

def test_rate_limiter_allows_under_limit():
    limiter = TokenBucketRateLimiter(rate=100.0, capacity=10)
    # Should allow 10 burst tokens immediately
    for _ in range(10):
        assert limiter.allow() is True
    # Next token should be denied
    assert limiter.allow() is False

def test_format_order_command_valid_add():
    cmd = format_order_command('A', 1001, 'B', 224.95, 500)
    assert cmd == "A,1001,B,224.95,500\n"

def test_format_order_command_valid_cancel():
    cmd = format_order_command('X', 1001, 'B', 0, 0)
    assert cmd == "X,1001,0,0,0\n"

def test_format_order_command_valid_exec():
    cmd = format_order_command('E', 1001, 'B', 225.00, 200)
    assert cmd == "E,1001,0,225.00,200\n"

def test_format_order_command_invalid_type():
    with pytest.raises(ValueError):
        format_order_command('Z', 1001, 'B', 100, 50)

def test_format_order_command_invalid_side():
    with pytest.raises(ValueError):
        format_order_command('A', 1001, 'INVALID', 100, 50)

def test_sanitize_and_route_command_order():
    valid, cmd_str, err = sanitize_and_route_command({
        "command": "ORDER",
        "type": "A",
        "orderId": 1005,
        "side": "S",
        "price": 225.50,
        "qty": 300
    })
    assert valid is True
    assert cmd_str == "A,1005,S,225.50,300\n"
    assert err == ""

def test_sanitize_and_route_command_raw_line():
    valid, cmd_str, err = sanitize_and_route_command({
        "command": "RAW_LINE",
        "line": "A,1001,B,100.00,500"
    })
    assert valid is True
    assert cmd_str == "A,1001,B,100.00,500\n"

def test_sanitize_and_route_command_batch():
    valid, cmd_str, err = sanitize_and_route_command({
        "command": "BATCH",
        "lines": [
            "A,1001,B,100.00,500",
            "A,1002,S,100.05,400",
            "X,1001,0,0,0"
        ]
    })
    assert valid is True
    assert "A,1001,B,100.00,500\n" in cmd_str
    assert "A,1002,S,100.05,400\n" in cmd_str

def test_sanitize_and_route_command_disallowed():
    valid, cmd_str, err = sanitize_and_route_command({
        "command": "DROP_DATABASE_INJECTION_ATTEMPT"
    })
    assert valid is False
    assert "Disallowed command" in err

def test_sanitize_and_route_command_path_traversal():
    valid, cmd_str, err = sanitize_and_route_command({
        "command": "FILE",
        "path": "../../etc/passwd"
    })
    assert valid is False
    assert "Disallowed" in err
