##
# Perl object that represents an Ext3 filesystem
#
# $Id$
##
package Permabit::FileSystem::Ext3;

use strict;
use warnings FATAL => qw(all);
use English qw(-no_match_vars);
use Permabit::Assertions qw(assertFalse assertNumArgs);
use Storable qw(dclone);

use base qw(Permabit::FileSystem);

#############################################################################
# @paramList{new}
my %PROPERTIES = (
                  # @ple we are a type ext3 fileSystem
                  fsType => "ext3",
                  # @ple the basic mkfs.ext3 options
                  mkfsOptions  => [ "-m 0", "-q", "-E nodiscard",],
                 );
##

#############################################################################
# Creates a C<Permabit::FileSystem::Ext3>.
#
# @params{new}
#
# @return a new C<Permabit::FileSystem::Ext3>
##
sub new {
  my $invocant = shift;
  return $invocant->SUPER::new(%{ dclone(\%PROPERTIES) },
                               # Overrides previous values
                               @_,);
}

#############################################################################
# @inherit
##
sub resizefs {
  my ($self) = assertNumArgs(1, @_);
  assertFalse($self->{exported});
  my $devPath = $self->{device}->getSymbolicPath();
  $self->{machine}->runSystemCmd("sudo resize2fs $devPath");
}

1;
