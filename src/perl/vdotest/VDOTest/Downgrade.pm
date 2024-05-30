##
# Verify that VDO cannot be downgraded.
#
# $Id$
##
package VDOTest::Downgrade;

use strict;
use warnings FATAL => qw(all);
use English qw(-no_match_vars);
use Log::Log4perl;

use Permabit::Assertions qw(
  assertNENumeric
  assertNumArgs
  assertTrue
);
use Permabit::Constants;

use base qw(VDOTest::UpgradeBase);

my $log = Log::Log4perl->get_logger(__PACKAGE__);

#############################################################################
# @paramList{getProperties}
our %PROPERTIES =
  (
   # @ple The scenario to start with
   initialScenario  => "X86_RHEL9_head",
  );
##

# This should be the oldest version currently supported by Upgrade tests.
my $PRIOR_SCENARIO = "X86_RHEL9_8.2.1-current";

#############################################################################
##
sub _failToDowngrade {
  my ($self) = assertNumArgs(1, @_);
  my $device = $self->getDevice();
  my $machine = $device->getMachine();

  # Install an older VDO.
  my $scenario = $self->generateScenarioHash($PRIOR_SCENARIO);
  $device->stop();
  $device->switchToScenario($scenario);

  # Start the VDO and expect a failure.
  my $kernLogSize = $machine->getKernLogSize();
  $device->runVDOCommand("start");
  assertNENumeric(0, $machine->getStatus());
  assertTrue($machine->searchKernLog($kernLogSize, "VDO_UNSUPPORTED_VERSION"));

  # Switch back to head for test cleanup.
  $scenario = $self->generateScenarioHash($self->{initialScenario});
  $device->switchToScenario($scenario);
}

#############################################################################
# Test downgrading from head to an older version would result in a failure.
##
sub testDowngrade {
  my ($self) = assertNumArgs(1, @_);
  $self->_failToDowngrade();
}

#############################################################################
# Test upgrading and then trying to go back down.
##
sub propertiesUpgradeDowngrade {
  my ($self) = assertNumArgs(1, @_);
  return ( initialScenario  => $PRIOR_SCENARIO );
}

#############################################################################
# Test upgrading and then trying to go back down.
##
sub testUpgradeDowngrade {
  my ($self) = assertNumArgs(1, @_);
  my $device = $self->getDevice();

  # Upgrade to a new VDO.
  $device->upgrade("head");
  $device->waitForIndex();

  $self->_failToDowngrade();
}

#############################################################################
# Test starting with a latest-prior VDO.
##
sub propertiesUpgradeDeviceNotBinaries {
  return ( initialScenario  => $PRIOR_SCENARIO );
}

#############################################################################
# Test upgrading the on-disk format to upgrade to head upon the next start,
# then trying to start with the older version.
##
sub testUpgradeDeviceNotBinaries {
  my ($self) = assertNumArgs(1, @_);
  my $device = $self->getDevice();

  $device->stop();
  $device->upgrade("head");
  $self->_failToDowngrade();
}

1;
