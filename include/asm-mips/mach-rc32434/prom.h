/*
 *  Definitions for the PROM
 *
 *  Copyright 2002 Ryan Holm <ryan.holmQVist@idt.com>
 *  Copyright 2008 Florian Fainelli <florian@openwrt.org>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#define PROM_ENTRY(x)		(0xbfc00000 + ((x) * 8))

#define GPIO_INIT_NOBUTTON	""
#define GPIO_INIT_BUTTON	" 2"

#define SR_NMI			0x00180000
#define SERIAL_SPEED_ENTRY	0x00000001

#define FREQ_TAG		"HZ="
#define GPIO_TAG		"gpio="
#define KMAC_TAG		"kmac="
#define MEM_TAG			"mem="
#define BOARD_TAG		"board="

#define BOARD_RB532		"500"
#define BOARD_RB532A		"500r5"
