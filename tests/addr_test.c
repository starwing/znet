/* addr_test.c -- address conversion helpers (znU_*).
 *
 * Covers znU_parseaddr (IPv4/IPv6 literals and rejected input),
 * znU_setinfo round-trips, znU_ipproto, znU_ntop and znU_error.  These are
 * pure string/struct conversions: no zn_State and no socket is needed.
 *
 * znU_parseaddr picks the family from a ':' in the string, so an IPv4
 * literal with a port glued on ("1.2.3.4:80") is parsed as IPv6 and fails.
 * That is why port 80 is passed separately everywhere below.
 */

#include "ztest.h"

/* ---- IPv4 ---- */

TEST(addr_parse_ipv4) {
    zn_SockAddr sa;
    const unsigned char loopback[4] = {127, 0, 0, 1};
    const unsigned char any[4] = {0, 0, 0, 0};
    const unsigned char bcast[4] = {255, 255, 255, 255};
    const unsigned char *portbytes;

    asserteq(znU_parseaddr(&sa, "127.0.0.1", 80), AF_INET);
    asserteq(znU_family(&sa), AF_INET);
    asserteq(znU_isv6(&sa), 0);
    asserteq(sa.ipv4.sin_port, htons(80));
    assertok(memcmp(&sa.ipv4.sin_addr, loopback, sizeof(loopback)) == 0);
    asserteq(znU_size(&sa), sizeof(struct sockaddr_in));

    /* the port is stored in network byte order, not host order */
    portbytes = (const unsigned char *)&sa.ipv4.sin_port;
    assertok(portbytes[0] == 0x00);
    assertok(portbytes[1] == 0x50);

    asserteq(znU_parseaddr(&sa, "0.0.0.0", 0), AF_INET);
    asserteq(znU_family(&sa), AF_INET);
    asserteq(sa.ipv4.sin_port, htons(0));
    assertok(memcmp(&sa.ipv4.sin_addr, any, sizeof(any)) == 0);

    asserteq(znU_parseaddr(&sa, "255.255.255.255", 65535), AF_INET);
    asserteq(znU_family(&sa), AF_INET);
    asserteq(sa.ipv4.sin_port, htons(65535));
    assertok(memcmp(&sa.ipv4.sin_addr, bcast, sizeof(bcast)) == 0);
}

TEST(addr_parse_ipv4_ports) {
    const unsigned ports[] = {0, 1, 80, 8080, 65535};
    zn_SockAddr sa;
    size_t i;

    for (i = 0; i < sizeof(ports) / sizeof(ports[0]); ++i) {
        asserteq(znU_parseaddr(&sa, "127.0.0.1", ports[i]), AF_INET);
        asserteq(znU_family(&sa), AF_INET);
        asserteq(sa.ipv4.sin_port, htons(ports[i]));
        /* the port does not disturb the address */
        assertok(sa.ipv4.sin_addr.s_addr == htonl(0x7F000001u));
    }
}

/* ---- IPv6 ---- */

TEST(addr_parse_ipv6) {
    zn_SockAddr sa;
    const unsigned char loopback6[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                         0, 0, 0, 0, 0, 0, 0, 1};
    const unsigned char doc6[16] = {0x20, 0x01, 0x0d, 0xb8, 0, 0, 0, 0,
                                    0, 0, 0, 0, 0, 0, 0, 1};

    asserteq(znU_parseaddr(&sa, "::1", 443), AF_INET6);
    asserteq(znU_family(&sa), AF_INET6);
    asserteq(znU_isv6(&sa), 1);
    asserteq(sa.ipv6.sin6_port, htons(443));
    assertok(memcmp(&sa.ipv6.sin6_addr, loopback6, sizeof(loopback6)) == 0);
    asserteq(znU_size(&sa), sizeof(struct sockaddr_in6));

    asserteq(znU_parseaddr(&sa, "2001:db8::1", 80), AF_INET6);
    asserteq(znU_family(&sa), AF_INET6);
    asserteq(znU_isv6(&sa), 1);
    asserteq(sa.ipv6.sin6_port, htons(80));
    assertok(memcmp(&sa.ipv6.sin6_addr, doc6, sizeof(doc6)) == 0);
    asserteq(znU_size(&sa), sizeof(struct sockaddr_in6));
}

/* ---- rejected input ---- */

TEST(addr_parse_invalid) {
    const char *bad[] = {
        "",
        "127.0.0",
        "127.0.0.1.5",
        "hello",
        "::g",
        "1.2.3.4:80", /* ':' selects IPv6, so this must be rejected */
    };
    zn_SockAddr sa;
    size_t i;

    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
        asserteq(znU_parseaddr(&sa, bad[i], 80), 0);
}

/* ---- setinfo round-trip ---- */

TEST(addr_setinfo_ipv4) {
    zn_SockAddr sa;
    zn_PeerInfo info;

    assertok(znU_parseaddr(&sa, "127.0.0.1", 1234) == AF_INET);
    memset(&info, 0x5A, sizeof(info));
    znU_setinfo(&sa, &info);
    assertstreq(info.addr, "127.0.0.1");
    asserteq(info.port, 1234u);
}

TEST(addr_setinfo_ipv6) {
    zn_SockAddr sa;
    zn_PeerInfo info;

    assertok(znU_parseaddr(&sa, "::1", 4321) == AF_INET6);
    memset(&info, 0x5A, sizeof(info));
    znU_setinfo(&sa, &info);
    assertstreq(info.addr, "::1");
    asserteq(info.port, 4321u);

    assertok(znU_parseaddr(&sa, "2001:db8::1", 80) == AF_INET6);
    znU_setinfo(&sa, &info);
    assertstreq(info.addr, "2001:db8::1");
    asserteq(info.port, 80u);
}

/* ---- socket type -> protocol ---- */

TEST(addr_ipproto) {
    asserteq(znU_ipproto(SOCK_STREAM), IPPROTO_TCP);
    asserteq(znU_ipproto(SOCK_DGRAM), IPPROTO_UDP);
}

/* ---- address -> text ---- */

TEST(addr_ntop) {
    char buf[ZN_MAX_ADDRLEN];
    const unsigned char v4[4] = {127, 0, 0, 1};
    const unsigned char v6[16] = {0, 0, 0, 0, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0, 1};

    memset(buf, 0x7F, sizeof(buf));
    assertok(znU_ntop(AF_INET, v4, buf, (socklen_t)sizeof(buf)) != NULL);
    assertstreq(buf, "127.0.0.1");

    memset(buf, 0x7F, sizeof(buf));
    assertok(znU_ntop(AF_INET6, v6, buf, (socklen_t)sizeof(buf)) != NULL);
    assertstreq(buf, "::1");
}

/* ---- system error -> zn_Error ----
 *
 * znU_error only has an errno table on POSIX; the Windows build maps WSA
 * codes, so the errno constants stay out of that build entirely. */

TEST(addr_error_mapping) {
#if defined(_WIN32)
    test_skip("znU_error maps WSA codes on Windows, errno codes are POSIX");
#else
    asserteq(znU_error(EPIPE), ZN_EHANGUP);
    asserteq(znU_error(ECONNABORTED), ZN_EHANGUP);
    asserteq(znU_error(ECONNREFUSED), ZN_EHANGUP);
    asserteq(znU_error(ECONNRESET), ZN_EHANGUP);
    asserteq(znU_error(EINVAL), ZN_ERROR);
#endif
}

#include "addr_test.gen.inc"
